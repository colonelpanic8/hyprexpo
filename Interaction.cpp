#include "Internals.hpp"

using namespace Hyprutils::String;
using namespace Internals;
using namespace std::chrono_literals;

Vector2D COverview::tilePointToWorkspacePoint(int id, const Vector2D& localPoint) const {
    const Vector2D tileSize = pMonitor->m_size / SIDE_LENGTH;
    const Vector2D tilePos  = tileSize * Vector2D{id % SIDE_LENGTH, id / SIDE_LENGTH};
    const Vector2D inTile   = localPoint - tilePos;

    return pMonitor->m_position + Vector2D{std::clamp(inTile.x / tileSize.x, 0.0, 1.0) * pMonitor->m_size.x, std::clamp(inTile.y / tileSize.y, 0.0, 1.0) * pMonitor->m_size.y};
}

PHLWINDOW COverview::windowAtTilePoint(int id, const Vector2D& localPoint) const {
    if (!isTileValid(id))
        return nullptr;

    const auto WORKSPACE = images[id].pWorkspace ? images[id].pWorkspace : g_pCompositor->getWorkspaceByID(images[id].workspaceID);
    if (!WORKSPACE)
        return nullptr;

    const auto POINT = tilePointToWorkspacePoint(id, localPoint);
    for (auto it = g_pCompositor->m_windows.rbegin(); it != g_pCompositor->m_windows.rend(); ++it) {
        const auto& window = *it;
        if (!windowVisibleOnWorkspace(window, WORKSPACE))
            continue;

        if (window->getWindowMainSurfaceBox().containsPoint(POINT))
            return window;
    }

    return nullptr;
}

void COverview::beginWindowDrag() {
    updateHoveredFromMouse();
    dragStartLocal = lastMousePosLocal;
    dragSourceID   = hoveredID;
    dragMoved      = false;
    dragWindow     = windowAtTilePoint(dragSourceID, dragStartLocal);
    dragGrabOffset = Vector2D{};

    if (dragWindow) {
        const auto POINT = tilePointToWorkspacePoint(dragSourceID, dragStartLocal);
        const auto BOX   = dragWindow->getWindowMainSurfaceBox();
        dragGrabOffset   = POINT - Vector2D{BOX.x, BOX.y};
        Cursor::overrideController->setOverride("grabbing", Cursor::CURSOR_OVERRIDE_UNKNOWN);
        damage();
    }
}

void COverview::updateWindowDrag() {
    if (!dragWindow || dragMoved)
        return;

    const auto DX = lastMousePosLocal.x - dragStartLocal.x;
    const auto DY = lastMousePosLocal.y - dragStartLocal.y;
    if (std::hypot(DX, DY) < 12.0)
        return;

    dragMoved = true;
    damage();
}

PHLWORKSPACE COverview::ensureWorkspaceForTile(int id) {
    if (!isTileValid(id))
        return nullptr;

    auto& image = images[id];
    if (image.pWorkspace)
        return image.pWorkspace;

    auto workspace = g_pCompositor->getWorkspaceByID(image.workspaceID);
    if (!workspace)
        workspace = g_pCompositor->createNewWorkspace(image.workspaceID, pMonitor->m_id, std::to_string(image.workspaceID), false);

    image.pWorkspace = workspace;
    return workspace;
}

bool COverview::finishWindowDrag() {
    const auto WINDOW = dragWindow;
    const int  SOURCE = dragSourceID;
    const bool MOVED  = dragMoved;

    dragWindow     = nullptr;
    dragSourceID   = -1;
    dragMoved      = false;
    dragGrabOffset = Vector2D{};
    Cursor::overrideController->setOverride("left_ptr", Cursor::CURSOR_OVERRIDE_UNKNOWN);

    if (!WINDOW)
        return false;

    if (!MOVED)
        return false;

    updateHoveredFromMouse();

    const int TARGET = hoveredID;
    if (!isTileValid(SOURCE) || !isTileValid(TARGET) || SOURCE == TARGET)
        return true;

    const auto SOURCEWS = images[SOURCE].pWorkspace ? images[SOURCE].pWorkspace : g_pCompositor->getWorkspaceByID(images[SOURCE].workspaceID);
    const auto TARGETWS = ensureWorkspaceForTile(TARGET);
    if (!windowVisibleOnWorkspace(WINDOW, SOURCEWS) || !TARGETWS || TARGETWS == SOURCEWS)
        return true;

    images[SOURCE].pWorkspace = SOURCEWS;
    g_pCompositor->moveWindowToWorkspaceSafe(WINDOW, TARGETWS);
    settleWorkspaceMoveAnimation(WINDOW);
    switchActiveWorkspaceToTile(TARGET);
    redrawDraggedWindowTiles(SOURCE, TARGET);
    return true;
}

int COverview::tileIDForVisibleIndex(size_t index) const {
    size_t visible = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (!isTileValid(i))
            continue;

        if (visible == index)
            return i;

        ++visible;
    }

    return -1;
}

bool COverview::moveWindowBetweenVisibleIndices(size_t sourceIndex, size_t targetIndex, const PHLWINDOW& requestedWindow) {
    if (closing)
        return false;

    const int SOURCE = tileIDForVisibleIndex(sourceIndex);
    const int TARGET = tileIDForVisibleIndex(targetIndex);
    if (!isTileValid(SOURCE) || !isTileValid(TARGET) || SOURCE == TARGET)
        return false;

    const auto SOURCEWS = images[SOURCE].pWorkspace ? images[SOURCE].pWorkspace : g_pCompositor->getWorkspaceByID(images[SOURCE].workspaceID);
    const auto TARGETWS = ensureWorkspaceForTile(TARGET);
    if (!SOURCEWS || !TARGETWS || SOURCEWS == TARGETWS)
        return false;

    PHLWINDOW window = requestedWindow;
    if (window) {
        if (!windowVisibleOnWorkspace(window, SOURCEWS))
            return false;
    } else {
        for (auto it = g_pCompositor->m_windows.rbegin(); it != g_pCompositor->m_windows.rend(); ++it) {
            const auto& candidate = *it;
            if (!windowVisibleOnWorkspace(candidate, SOURCEWS))
                continue;

            window = candidate;
            break;
        }
    }

    if (!window)
        return false;

    images[SOURCE].pWorkspace = SOURCEWS;
    g_pCompositor->moveWindowToWorkspaceSafe(window, TARGETWS);
    settleWorkspaceMoveAnimation(window);
    switchActiveWorkspaceToTile(TARGET);
    redrawDraggedWindowTiles(SOURCE, TARGET);
    return true;
}

void COverview::switchActiveWorkspaceToTile(int target) {
    if (!isTileValid(target) || !pMonitor)
        return;

    const auto TARGETWS = ensureWorkspaceForTile(target);
    if (!TARGETWS)
        return;

    openedID  = target;
    closeOnID = target;
    kbFocusID = target;
    startedOn = TARGETWS;

    pMonitor->setSpecialWorkspace(0);

    if (pMonitor->m_activeWorkspace != TARGETWS)
        Config::Actions::changeWorkspace(TARGETWS->getConfigName());

    if (pMonitor->m_activeWorkspace)
        startedOn = pMonitor->m_activeWorkspace;

    damageDirty = true;
    damage();
}

void COverview::redrawDraggedWindowTiles(int source, int target) {
    queueRedrawID(source);
    queueRedrawID(target);

    for (const auto id : queuedRedrawIDs) {
        if (std::ranges::find(settlingRedrawIDs, id) == settlingRedrawIDs.end())
            settlingRedrawIDs.push_back(id);
    }
    redrawSettleTicks = 4;

    flushQueuedRedraws();

    if (redrawSettleTimer)
        return;

    redrawSettleTimer = makeShared<CEventLoopTimer>(
        75ms,
        [this](SP<CEventLoopTimer> self, void*) {
            if (!g_pOverview || g_pOverview.get() != this || closing) {
                self->cancel();
                redrawSettleTimer.reset();
                return;
            }

            for (const auto id : settlingRedrawIDs)
                queueRedrawID(id);

            flushQueuedRedraws();

            if (--redrawSettleTicks <= 0) {
                settlingRedrawIDs.clear();
                redrawSettleTimer.reset();
                self->cancel();
                return;
            }

            self->updateTimeout(100ms);
        },
        nullptr);
    g_pEventLoopManager->addTimer(redrawSettleTimer);
}

void COverview::queueRedrawID(int id) {
    if (!isTileValid(id))
        return;

    if (std::ranges::find(queuedRedrawIDs, id) == queuedRedrawIDs.end())
        queuedRedrawIDs.push_back(id);
}

void COverview::flushQueuedRedraws() {
    if (queuedRedrawIDs.empty())
        return;

    const auto IDS = queuedRedrawIDs;
    queuedRedrawIDs.clear();

    for (const auto id : IDS)
        redrawID(id);

    damage();
}

void COverview::followFocusToTile(int target) {
    static const CConfigValue<Config::INTEGER> PFOLLOWFOCUS("plugin:hyprexpo:live_preview_follow_focus");

    if (*PFOLLOWFOCUS && isTileValid(target) && openedID != target)
        switchActiveWorkspaceToTile(target);
}

void COverview::selectHoveredWorkspace() {
    if (closing)
        return;

    updateHoveredFromMouse();
    closeOnID = std::clamp(hoveredID, 0, SIDE_LENGTH * SIDE_LENGTH - 1);
}

bool COverview::selectWorkspaceByID(int64_t workspaceID) {
    if (closing)
        return false;

    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID != workspaceID)
            continue;

        closeOnID = i;
        return true;
    }

    return false;
}

bool COverview::selectVisibleIndex(size_t index) {
    if (closing)
        return false;

    size_t visible = 0;
    for (size_t i = 0; i < images.size(); ++i) {
        if (images[i].workspaceID == WORKSPACE_INVALID)
            continue;

        if (visible == index) {
            closeOnID = i;
            return true;
        }

        ++visible;
    }

    return false;
}

bool COverview::selectVisibleToken(const std::string& token) {
    if (closing)
        return false;

    static const CConfigValue<Config::INTEGER> PSELECTLABEL("plugin:hyprexpo:selection_label_enable");
    static const CConfigValue<Config::STRING>  PSELECTTOKENMAP("plugin:hyprexpo:selection_label_token_map");

    const auto                                 TOKEN = lowerCopy(token);
    if (*PSELECTLABEL) {
        const std::string tokenMapConfig = *PSELECTTOKENMAP;
        const auto        tokenMap       = tokenMapConfig.empty() ? std::vector<std::string>{} : splitCommaList(tokenMapConfig);

        for (size_t i = 0; i < tokenMap.size(); ++i) {
            if (tokenMap[i].empty() || lowerCopy(tokenMap[i]) != TOKEN)
                continue;

            return selectVisibleIndex(i);
        }

        return false;
    }

    const auto index = fallbackTokenToVisibleIndex(TOKEN);
    return index && selectVisibleIndex(*index);
}

bool COverview::isTileValid(int id) const {
    return id >= 0 && id < (int)images.size() && images[id].workspaceID != WORKSPACE_INVALID;
}

void COverview::updateHoveredFromMouse() {
    if (!pMonitor)
        return;

    const int x = std::clamp((int)(lastMousePosLocal.x / pMonitor->m_size.x * SIDE_LENGTH), 0, SIDE_LENGTH - 1);
    const int y = std::clamp((int)(lastMousePosLocal.y / pMonitor->m_size.y * SIDE_LENGTH), 0, SIDE_LENGTH - 1);

    const int newHoveredID = std::clamp(x + y * SIDE_LENGTH, 0, SIDE_LENGTH * SIDE_LENGTH - 1);
    if (newHoveredID == hoveredID)
        return;

    const int oldHoveredID = hoveredID;
    hoveredID              = newHoveredID;

    if (oldHoveredID != -1)
        followFocusToTile(hoveredID);

    damage();
}

void COverview::ensureKeyboardFocus() {
    if (isTileValid(kbFocusID))
        return;

    if (isTileValid(openedID)) {
        kbFocusID = openedID;
        return;
    }

    for (size_t i = 0; i < images.size(); ++i) {
        if (!isTileValid(i))
            continue;

        kbFocusID = i;
        return;
    }
}

void COverview::moveKeyboardFocus(int dx, int dy) {
    if (closing)
        return;

    static const CConfigValue<Config::INTEGER> PWRAPH("plugin:hyprexpo:keynav_wrap_h");
    static const CConfigValue<Config::INTEGER> PWRAPV("plugin:hyprexpo:keynav_wrap_v");
    static const CConfigValue<Config::INTEGER> PREADING("plugin:hyprexpo:keynav_reading_order");

    ensureKeyboardFocus();
    if (!isTileValid(kbFocusID))
        return;

    const int total = SIDE_LENGTH * SIDE_LENGTH;
    const int x     = kbFocusID % SIDE_LENGTH;
    const int y     = kbFocusID / SIDE_LENGTH;

    if (dx != 0 && *PREADING) {
        const int step = dx > 0 ? 1 : -1;
        int       id   = kbFocusID;
        for (int tries = 0; tries < total; ++tries) {
            id += step;
            if (id < 0 || id >= total) {
                if (*PWRAPH && *PWRAPV)
                    id = (id + total) % total;
                else
                    break;
            }

            if (isTileValid(id)) {
                kbFocusID = id;
                followFocusToTile(kbFocusID);
                damage();
                return;
            }
        }
    }

    if (dx != 0) {
        const int step = dx > 0 ? 1 : -1;
        int       nx   = x;
        for (int tries = 0; tries < SIDE_LENGTH; ++tries) {
            nx += step;
            if (nx < 0 || nx >= SIDE_LENGTH) {
                if (*PWRAPH)
                    nx = (nx + SIDE_LENGTH) % SIDE_LENGTH;
                else
                    break;
            }

            const int id = nx + y * SIDE_LENGTH;
            if (isTileValid(id)) {
                kbFocusID = id;
                followFocusToTile(kbFocusID);
                damage();
                return;
            }
        }
    }

    if (dy != 0) {
        const int step = dy > 0 ? 1 : -1;
        int       ny   = y;
        for (int tries = 0; tries < SIDE_LENGTH; ++tries) {
            ny += step;
            if (ny < 0 || ny >= SIDE_LENGTH) {
                if (*PWRAPV)
                    ny = (ny + SIDE_LENGTH) % SIDE_LENGTH;
                else
                    break;
            }

            const int id = x + ny * SIDE_LENGTH;
            if (isTileValid(id)) {
                kbFocusID = id;
                followFocusToTile(kbFocusID);
                damage();
                return;
            }
        }
    }
}

void COverview::confirmKeyboardFocus() {
    if (closing)
        return;

    ensureKeyboardFocus();
    if (isTileValid(kbFocusID))
        closeOnID = kbFocusID;
    close();
}
