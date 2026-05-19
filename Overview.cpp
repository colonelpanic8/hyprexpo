#include "Overview.hpp"
#include "LabelRenderer.hpp"
#include "WorkspaceLayout.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#define private   public
#define protected public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/helpers/Format.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/cursor/CursorShapeOverrideController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#undef private
#undef protected
#include "OverviewPassElement.hpp"

using namespace Hyprutils::String;
using namespace std::chrono_literals;

static std::string lowerCopy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

static std::string fallbackTokenForVisibleIndex(size_t visible) {
    if (visible < 9)
        return std::to_string(visible + 1);
    if (visible == 9)
        return "0";
    if (visible < 36)
        return std::string(1, (char)('a' + visible - 10));

    return "";
}

static std::optional<size_t> fallbackTokenToVisibleIndex(const std::string& token) {
    if (token.size() != 1)
        return std::nullopt;

    const char c = token[0];
    if (c >= '1' && c <= '9')
        return c - '1';
    if (c == '0')
        return 9;
    if (c >= 'a' && c <= 'z')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'Z')
        return 10 + c - 'A';

    return std::nullopt;
}

static uint32_t framebufferFormatWithAlpha(uint32_t drmFormat) {
    const auto alphaFormat = NFormatUtils::alphaFormat(drmFormat);
    return alphaFormat == 0 ? DRM_FORMAT_ABGR8888 : alphaFormat;
}

static void clearWithColor(const CHyprColor& color) {
    glClearColor(color.r, color.g, color.b, color.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

static Config::CGradientValueData gradientFromColor(uint64_t color) {
    Config::CGradientValueData grad{CHyprColor(color)};
    grad.updateColorsOk();
    return grad;
}

static bool windowVisibleOnWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
    return window && workspace && window->m_workspace == workspace && window->m_isMapped && !window->isHidden() && !window->m_pinned;
}

static void settleWorkspaceMoveAnimation(const PHLWINDOW& window) {
    if (!window)
        return;

    window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->resetAllCallbacks();
    window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->setValueAndWarp(1.F);
    *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE) = 1.F;
    window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->setValueAndWarp(1.F);
    *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE) = 1.F;
    window->m_monitorMovedFrom = -1;
}

static void ensureFramebuffer(COverview::SWorkspaceImage& image, const CBox& monbox, uint32_t drmFormat) {
    if (!image.fb)
        image.fb = g_pHyprRenderer->createFB("hyprexpo");

    if (image.fb->m_size != monbox.size()) {
        image.fb->release();
        image.fb->alloc(monbox.w, monbox.h, drmFormat);
    }
}

static void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
    g_pOverview->damage();
}

struct SWorkspacePreviewState {
    bool     visible        = false;
    bool     forceRendering = false;
    float    alphaValue     = 1.F;
    float    alphaGoal      = 1.F;
    Vector2D offsetValue;
    Vector2D offsetGoal;
};

struct SWindowPreviewState {
    PHLWINDOW window;
    Vector2D  positionValue;
    Vector2D  positionGoal;
    Vector2D  sizeValue;
    Vector2D  sizeGoal;
};

static SWorkspacePreviewState applyWorkspacePreviewState(const PHLWORKSPACE& workspace) {
    SWorkspacePreviewState state;
    if (!workspace)
        return state;

    state.visible        = workspace->m_visible;
    state.forceRendering = workspace->m_forceRendering;
    state.alphaValue     = workspace->m_alpha->value();
    state.alphaGoal      = workspace->m_alpha->goal();
    state.offsetValue    = workspace->m_renderOffset->value();
    state.offsetGoal     = workspace->m_renderOffset->goal();

    workspace->m_visible        = true;
    workspace->m_forceRendering = true;
    workspace->m_alpha->setValueAndWarp(1.F);
    *workspace->m_alpha = 1.F;
    workspace->m_renderOffset->setValueAndWarp(Vector2D{});
    *workspace->m_renderOffset = Vector2D{};

    return state;
}

static void restoreWorkspacePreviewState(const PHLWORKSPACE& workspace, const SWorkspacePreviewState& state) {
    if (!workspace)
        return;

    workspace->m_visible        = state.visible;
    workspace->m_forceRendering = state.forceRendering;
    workspace->m_alpha->setValueAndWarp(state.alphaValue);
    *workspace->m_alpha = state.alphaGoal;
    workspace->m_renderOffset->setValueAndWarp(state.offsetValue);
    *workspace->m_renderOffset = state.offsetGoal;
}

static std::vector<SWindowPreviewState> applyWorkspaceWindowPreviewState(const PHLWORKSPACE& workspace) {
    std::vector<SWindowPreviewState> states;
    if (!workspace)
        return states;

    for (const auto& window : g_pCompositor->m_windows) {
        if (!windowVisibleOnWorkspace(window, workspace))
            continue;

        states.push_back({
            .window        = window,
            .positionValue = window->m_realPosition->value(),
            .positionGoal  = window->m_realPosition->goal(),
            .sizeValue     = window->m_realSize->value(),
            .sizeGoal      = window->m_realSize->goal(),
        });

        window->m_realPosition->setValueAndWarp(window->m_realPosition->goal());
        window->m_realSize->setValueAndWarp(window->m_realSize->goal());
    }

    return states;
}

static void restoreWorkspaceWindowPreviewState(const std::vector<SWindowPreviewState>& states) {
    for (const auto& state : states) {
        if (!state.window)
            continue;

        state.window->m_realPosition->setValueAndWarp(state.positionValue);
        *state.window->m_realPosition = state.positionGoal;
        state.window->m_realSize->setValueAndWarp(state.sizeValue);
        *state.window->m_realSize = state.sizeGoal;
    }
}

static void recalculateWorkspaceForPreview(PHLMONITOR monitor, const PHLWORKSPACE& workspace) {
    if (!monitor || !workspace || !g_layoutManager)
        return;

    const auto STARTEDON = monitor->m_activeWorkspace;

    // Fake framebuffer captures bypass Hyprland's normal monitor-render recalc,
    // which only updates the monitor's active workspace.
    monitor->m_activeWorkspace = workspace;
    g_layoutManager->recalculateMonitor(monitor);
    monitor->m_activeWorkspace = STARTEDON;
}

COverview::~COverview() {
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    images.clear(); // otherwise we get a vram leak
    Cursor::overrideController->unsetOverride(Cursor::CURSOR_OVERRIDE_UNKNOWN);
    g_pPointerManager->resetCursorImage();
    g_pInputManager->simulateMouseMovement();
    if (pMonitor)
        pMonitor->m_blurFBDirty = true;
}

COverview::COverview(PHLWORKSPACE startedOn_, bool swipe_) : startedOn(startedOn_), swipe(swipe_) {
    const auto PMONITOR = Desktop::focusState()->monitor();
    pMonitor            = PMONITOR;

    static const CConfigValue<Config::INTEGER> PCOLUMNS("plugin:hyprexpo:columns");
    static const CConfigValue<Config::INTEGER> PGAPS("plugin:hyprexpo:gap_size");
    static const CConfigValue<Config::INTEGER> PCOL("plugin:hyprexpo:bg_col");
    static const CConfigValue<Config::INTEGER> PSKIP("plugin:hyprexpo:skip_empty");
    static const CConfigValue<Config::INTEGER> PMAXWS("plugin:hyprexpo:max_workspace");
    static const CConfigValue<Config::INTEGER> PSHOWNUM("plugin:hyprexpo:show_workspace_numbers");
    static const CConfigValue<Config::STRING>  PMETHOD("plugin:hyprexpo:workspace_method");

    SIDE_LENGTH          = *PCOLUMNS;
    GAP_WIDTH            = *PGAPS;
    BG_COLOR             = CHyprColor(*PCOL);
    showWorkspaceNumbers = *PSHOWNUM;

    const auto [methodCenter, methodStartID] = workspaceMethodForMonitor(pMonitor.lock(), *PMETHOD);

    images.resize(SIDE_LENGTH * SIDE_LENGTH);

    // r includes empty workspaces; m skips over them
    const bool    skipEmpty    = *PSKIP;
    const int64_t maxWorkspace = *PMAXWS;
    std::string   selector     = skipEmpty ? "m" : "r";

    if (!skipEmpty && maxWorkspace > 0) {
        const int64_t tileCount = SIDE_LENGTH * SIDE_LENGTH;
        const int64_t maxStart  = std::max<int64_t>(1, maxWorkspace - tileCount + 1);
        const int64_t startID   = methodCenter ? std::clamp<int64_t>(methodStartID - tileCount / 2, 1, maxStart) : std::clamp<int64_t>(methodStartID, 1, maxStart);

        for (size_t i = 0; i < images.size(); ++i) {
            const int64_t workspaceID = startID + i;
            images[i].workspaceID     = workspaceID <= maxWorkspace ? workspaceID : WORKSPACE_INVALID;
        }
    } else if (methodCenter) {
        int currentID = methodStartID;
        int firstID   = currentID;

        int backtracked = 0;

        // Initialize tiles to WORKSPACE_INVALID; cliking one of these results
        // in changing to "emptynm" (next empty workspace). Tiles with this id
        // will only remain if skip_empty is on.
        for (size_t i = 0; i < images.size(); i++) {
            images[i].workspaceID = WORKSPACE_INVALID;
        }

        // Scan through workspaces lower than methodStartID until we wrap; count how many
        for (size_t i = 1; i < images.size() / 2; ++i) {
            currentID = getWorkspaceIDNameFromString(selector + "-" + std::to_string(i)).id;
            if (currentID >= firstID)
                break;

            backtracked++;
            firstID = currentID;
        }

        // Scan through workspaces higher than methodStartID. If using "m"
        // (skip_empty), stop when we wrap, leaving the rest of the workspace
        // ID's set to WORKSPACE_INVALID
        for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
            auto& image = images[i];
            if ((int64_t)i - backtracked < 0) {
                currentID = getWorkspaceIDNameFromString(selector + std::to_string((int64_t)i - backtracked)).id;
            } else {
                currentID = getWorkspaceIDNameFromString(selector + "+" + std::to_string((int64_t)i - backtracked)).id;
                if (i > 0 && currentID == firstID)
                    break;
            }
            image.workspaceID = currentID;
        }

    } else {
        int currentID         = methodStartID;
        images[0].workspaceID = currentID;

        auto PWORKSPACESTART = g_pCompositor->getWorkspaceByID(currentID);
        if (!PWORKSPACESTART)
            PWORKSPACESTART = CWorkspace::create(currentID, pMonitor.lock(), std::to_string(currentID));

        pMonitor->m_activeWorkspace = PWORKSPACESTART;

        // Scan through workspaces higher than methodStartID. If using "m"
        // (skip_empty), stop when we wrap, leaving the rest of the workspace
        // ID's set to WORKSPACE_INVALID
        for (size_t i = 1; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
            auto& image = images[i];
            currentID   = getWorkspaceIDNameFromString(selector + "+" + std::to_string(i)).id;
            if (currentID <= methodStartID)
                break;
            image.workspaceID = currentID;
        }

        pMonitor->m_activeWorkspace = startedOn;
    }

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    Vector2D tileSize       = pMonitor->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH * pMonitor->m_scale, GAP_WIDTH * pMonitor->m_scale} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    CBox     monbox{0, 0, tileSize.x * 2, tileSize.y * 2};

    if (!ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    int          currentid = 0;

    PHLWORKSPACE openSpecial = PMONITOR->m_activeSpecialWorkspace;
    if (openSpecial)
        PMONITOR->m_activeSpecialWorkspace.reset();

    g_pHyprRenderer->m_bBlockSurfaceFeedback = true;

    startedOn->m_visible = false;

    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        COverview::SWorkspaceImage& image = images[i];
        ensureFramebuffer(image, monbox, framebufferFormatWithAlpha(PMONITOR->m_output->state->state().drmFormat));

        CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
        g_pHyprRenderer->beginRender(PMONITOR, fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, image.fb);

        clearWithColor(CHyprColor{0, 0, 0, 1.0});

        const auto PWORKSPACE = g_pCompositor->getWorkspaceByID(image.workspaceID);

        if (PWORKSPACE == startedOn)
            currentid = i;

        if (PWORKSPACE) {
            image.pWorkspace            = PWORKSPACE;
            PMONITOR->m_activeWorkspace = PWORKSPACE;
            const auto PREVIEWSTATE     = applyWorkspacePreviewState(PWORKSPACE);
            recalculateWorkspaceForPreview(PMONITOR, PWORKSPACE);
            const auto WINDOWPREVIEWSTATE = PWORKSPACE == startedOn ? std::vector<SWindowPreviewState>{} : applyWorkspaceWindowPreviewState(PWORKSPACE);

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace = openSpecial;

            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

            restoreWorkspaceWindowPreviewState(WINDOWPREVIEWSTATE);
            restoreWorkspacePreviewState(PWORKSPACE, PREVIEWSTATE);

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace.reset();
        } else
            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

        image.box = {(i % SIDE_LENGTH) * tileRenderSize.x + (i % SIDE_LENGTH) * GAP_WIDTH, (i / SIDE_LENGTH) * tileRenderSize.y + (i / SIDE_LENGTH) * GAP_WIDTH, tileRenderSize.x,
                     tileRenderSize.y};

        g_pHyprRenderer->m_renderData.blockScreenShader = true;
        g_pHyprRenderer->endRender();
    }

    g_pHyprRenderer->m_bBlockSurfaceFeedback = false;

    PMONITOR->m_activeSpecialWorkspace = openSpecial;
    PMONITOR->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);

    g_pAnimationManager->createAnimation(pMonitor->m_size * pMonitor->m_size / tileSize, size, Config::animationTree()->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);
    g_pAnimationManager->createAnimation((-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{currentid % SIDE_LENGTH, currentid / SIDE_LENGTH}) * pMonitor->m_scale) *
                                             (pMonitor->m_size / tileSize),
                                         pos, Config::animationTree()->getAnimationPropertyConfig("windowsMove"), AVARDAMAGE_NONE);

    size->setUpdateCallback(damageMonitor);
    pos->setUpdateCallback(damageMonitor);

    if (!swipe) {
        *size = pMonitor->m_size;
        *pos  = {0, 0};

        size->setCallbackOnEnd([this](auto) { redrawAll(true); });
    }

    openedID = currentid;

    Cursor::overrideController->setOverride("left_ptr", Cursor::CURSOR_OVERRIDE_UNKNOWN);

    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
    updateHoveredFromMouse();
    kbFocusID = openedID;

    auto onCursorMove = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled    = true;
        lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
        updateHoveredFromMouse();
        updateWindowDrag();
    };

    auto onCursorSelect = [this](IPointer::SButtonEvent event, Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled = true;

        if (event.state == WL_POINTER_BUTTON_STATE_PRESSED) {
            beginWindowDrag();
            return;
        }

        if (finishWindowDrag())
            return;

        selectHoveredWorkspace();

        close();
    };

    auto onTouchSelect = [this](Event::SCallbackInfo& info) {
        if (closing)
            return;

        info.cancelled = true;

        selectHoveredWorkspace();

        close();
    };

    mouseMoveHook = Event::bus()->m_events.input.mouse.move.listen([onCursorMove](Vector2D, Event::SCallbackInfo& info) { onCursorMove(info); });
    touchMoveHook = Event::bus()->m_events.input.touch.motion.listen([onCursorMove](ITouch::SMotionEvent, Event::SCallbackInfo& info) { onCursorMove(info); });

    mouseButtonHook = Event::bus()->m_events.input.mouse.button.listen([onCursorSelect](IPointer::SButtonEvent event, Event::SCallbackInfo& info) { onCursorSelect(event, info); });
    touchDownHook   = Event::bus()->m_events.input.touch.down.listen([onTouchSelect](ITouch::SDownEvent, Event::SCallbackInfo& info) { onTouchSelect(info); });
}

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

    dragWindow = nullptr;
    dragSourceID = -1;
    dragMoved    = false;
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
    redrawDraggedWindowTiles(SOURCE, TARGET);
    return true;
}

void COverview::redrawDraggedWindowTiles(int source, int target) {
    auto refresh = [source, target] {
        if (!g_pOverview || g_pOverview->closing)
            return;

        g_pOverview->redrawID(source);
        g_pOverview->redrawID(target);
        g_pOverview->damage();
    };

    refresh();

    auto count = std::make_shared<int>(0);
    auto timer = makeShared<CEventLoopTimer>(50ms, [source, target, count](SP<CEventLoopTimer> self, void*) {
        if (!g_pOverview || g_pOverview->closing) {
            self->cancel();
            return;
        }

        g_pOverview->redrawID(source);
        g_pOverview->redrawID(target);
        g_pOverview->damage();

        if (++*count >= 3) {
            self->cancel();
            return;
        }

        self->updateTimeout(100ms);
    }, nullptr);
    g_pEventLoopManager->addTimer(timer);
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

    hoveredID = newHoveredID;
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

void COverview::redrawID(int id, bool forcelowres) {
    if (!pMonitor)
        return;

    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    blockOverviewRendering = true;

    Render::GL::g_pHyprOpenGL->makeEGLCurrent();

    id = std::clamp(id, 0, SIDE_LENGTH * SIDE_LENGTH - 1);

    Vector2D tileSize       = pMonitor->m_size / SIDE_LENGTH;
    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH, GAP_WIDTH} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;
    CBox     monbox{0, 0, tileSize.x * 2, tileSize.y * 2};

    if (!forcelowres && (size->value() != pMonitor->m_size || closing))
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    if (!ENABLE_LOWRES)
        monbox = {{0, 0}, pMonitor->m_pixelSize};

    auto& image = images[id];

    ensureFramebuffer(image, monbox, framebufferFormatWithAlpha(pMonitor->m_output->state->state().drmFormat));

    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, Render::RENDER_MODE_FULL_FAKE, nullptr, image.fb);

    clearWithColor(CHyprColor{0, 0, 0, 1.0});

    const auto   PWORKSPACE = image.pWorkspace ? image.pWorkspace : g_pCompositor->getWorkspaceByID(image.workspaceID);
    image.pWorkspace        = PWORKSPACE;

    PHLWORKSPACE openSpecial = pMonitor->m_activeSpecialWorkspace;
    if (openSpecial)
        pMonitor->m_activeSpecialWorkspace.reset();

    startedOn->m_visible = false;

    if (PWORKSPACE) {
        pMonitor->m_activeWorkspace    = PWORKSPACE;
        const auto PREVIEWSTATE        = applyWorkspacePreviewState(PWORKSPACE);
        recalculateWorkspaceForPreview(pMonitor.lock(), PWORKSPACE);
        const auto WINDOWPREVIEWSTATE = PWORKSPACE == startedOn ? std::vector<SWindowPreviewState>{} : applyWorkspaceWindowPreviewState(PWORKSPACE);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace = openSpecial;

        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

        restoreWorkspaceWindowPreviewState(WINDOWPREVIEWSTATE);
        restoreWorkspacePreviewState(PWORKSPACE, PREVIEWSTATE);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace.reset();
    } else
        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

    g_pHyprRenderer->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    pMonitor->m_activeSpecialWorkspace = openSpecial;
    pMonitor->m_activeWorkspace        = startedOn;
    startedOn->m_visible               = true;
    g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);

    blockOverviewRendering = false;
}

void COverview::redrawAll(bool forcelowres) {
    if (!pMonitor)
        return;
    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        redrawID(i, forcelowres);
    }
}

void COverview::damage() {
    blockDamageReporting = true;
    g_pHyprRenderer->damageMonitor(pMonitor.lock());
    blockDamageReporting = false;
}

void COverview::onDamageReported() {
    damageDirty = true;

    Vector2D                                   SIZE = size->value();

    static const CConfigValue<Config::INTEGER> PGAPOUT("plugin:hyprexpo:gap_size_outer");

    const auto                                 GAPSIZE        = (closing ? (1.0 - size->getPercent()) : size->getPercent()) * GAP_WIDTH;
    const auto                                 OUTER          = *PGAPOUT * (closing ? (1.0 - size->getPercent()) : size->getPercent());
    Vector2D                                   tileRenderSize = (SIZE - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1) - Vector2D{OUTER * 2, OUTER * 2}) / SIDE_LENGTH;
    CBox                                       texbox         = CBox{OUTER + (openedID % SIDE_LENGTH) * tileRenderSize.x + (openedID % SIDE_LENGTH) * GAPSIZE,
                       OUTER + (openedID / SIDE_LENGTH) * tileRenderSize.y + (openedID / SIDE_LENGTH) * GAPSIZE, tileRenderSize.x, tileRenderSize.y}
                      .translate(pMonitor->m_position);

    damage();

    blockDamageReporting = true;
    g_pHyprRenderer->damageBox(texbox);
    blockDamageReporting = false;
    g_pCompositor->scheduleFrameForMonitor(pMonitor.lock());
}

int64_t COverview::selectedWorkspaceID() const {
    const int ID = closeOnID == -1 ? openedID : closeOnID;
    if (ID < 0 || ID >= (int)images.size())
        return WORKSPACE_INVALID;

    return images[ID].workspaceID;
}

void COverview::close(bool switchToSelection) {
    if (closing)
        return;

    const int   ID     = closeOnID == -1 ? openedID : closeOnID;
    const int   SAFEID = std::clamp(ID, 0, SIDE_LENGTH * SIDE_LENGTH - 1);

    const auto& TILE = images[SAFEID];

    Vector2D    tileSize = (pMonitor->m_size / SIDE_LENGTH);

    size->warp();
    pos->warp();

    *size = pMonitor->m_size * pMonitor->m_size / tileSize;
    *pos  = (-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{SAFEID % SIDE_LENGTH, SAFEID / SIDE_LENGTH}) * pMonitor->m_scale) * (pMonitor->m_size / tileSize);

    closing = true;

    redrawAll();

    if (switchToSelection && TILE.workspaceID != pMonitor->activeWorkspaceID()) {
        pMonitor->setSpecialWorkspace(0);

        // If this tile's workspace was WORKSPACE_INVALID, move to the next
        // empty workspace. This should only happen if skip_empty is on, in
        // which case some tiles will be left with this ID intentionally.
        const int  NEWID = TILE.workspaceID == WORKSPACE_INVALID ? getWorkspaceIDNameFromString("emptynm").id : TILE.workspaceID;

        const auto NEWIDWS = g_pCompositor->getWorkspaceByID(NEWID);

        const auto OLDWS = pMonitor->m_activeWorkspace;

        if (!NEWIDWS)
            Config::Actions::changeWorkspace(std::to_string(NEWID));
        else
            Config::Actions::changeWorkspace(NEWIDWS->getConfigName());

        g_pDesktopAnimationManager->startAnimation(pMonitor->m_activeWorkspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
        g_pDesktopAnimationManager->startAnimation(OLDWS, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);

        startedOn = pMonitor->m_activeWorkspace;
    }

    size->setCallbackOnEnd([](auto) { g_pEventLoopManager->doLater([] { g_pOverview.reset(); }); });
}

void COverview::onPreRender() {
    if (damageDirty) {
        damageDirty = false;
        redrawID(closing ? (closeOnID == -1 ? openedID : closeOnID) : openedID);
    }
}

void COverview::onWorkspaceChange() {
    if (valid(startedOn))
        g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false, true);
    else
        startedOn = pMonitor->m_activeWorkspace;

    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        if (images[i].workspaceID != pMonitor->activeWorkspaceID())
            continue;

        openedID = i;
        break;
    }

    closeOnID = openedID;
    if (!closing)
        close();
}

void COverview::render() {
    g_pHyprRenderer->m_renderPass.add(makeUnique<COverviewPassElement>());
}

void COverview::fullRender() {
    const auto GAPSIZE = (closing ? (1.0 - size->getPercent()) : size->getPercent()) * GAP_WIDTH;

    if (pMonitor->m_activeWorkspace != startedOn && !closing) {
        // likely user changed.
        onWorkspaceChange();
    }

    Vector2D                                   SIZE = size->value();

    static const CConfigValue<Config::INTEGER> PGAPOUT("plugin:hyprexpo:gap_size_outer");
    static const CConfigValue<Config::INTEGER> PTILEROUND("plugin:hyprexpo:tile_rounding");
    static const CConfigValue<Config::FLOAT>   PROUNDINGPOWER("plugin:hyprexpo:tile_rounding_power");
    static const CConfigValue<Config::INTEGER> PTILEROUNDHOVER("plugin:hyprexpo:tile_rounding_hover");
    static const CConfigValue<Config::INTEGER> PTILEROUNDFOCUS("plugin:hyprexpo:tile_rounding_focus");
    static const CConfigValue<Config::INTEGER> PTILEROUNDCURRENT("plugin:hyprexpo:tile_rounding_current");
    static const CConfigValue<Config::INTEGER> PLABELENABLE("plugin:hyprexpo:label_enable");
    static const CConfigValue<Config::STRING>  PLABELMODE("plugin:hyprexpo:label_text_mode");
    static const CConfigValue<Config::STRING>  PLABELTOKENMAP("plugin:hyprexpo:label_token_map");
    static const CConfigValue<Config::STRING>  PLABELPOS("plugin:hyprexpo:label_position");
    static const CConfigValue<Config::INTEGER> PLABELOFFX("plugin:hyprexpo:label_offset_x");
    static const CConfigValue<Config::INTEGER> PLABELOFFY("plugin:hyprexpo:label_offset_y");
    static const CConfigValue<Config::INTEGER> PSELECTLABELENABLE("plugin:hyprexpo:selection_label_enable");
    static const CConfigValue<Config::STRING>  PSELECTLABELTOKENS("plugin:hyprexpo:selection_label_token_map");
    static const CConfigValue<Config::STRING>  PSELECTLABELPOS("plugin:hyprexpo:selection_label_position");
    static const CConfigValue<Config::INTEGER> PSELECTLABELOFFX("plugin:hyprexpo:selection_label_offset_x");
    static const CConfigValue<Config::INTEGER> PSELECTLABELOFFY("plugin:hyprexpo:selection_label_offset_y");
    static const CConfigValue<Config::INTEGER> PSELECTLABELCOL("plugin:hyprexpo:selection_label_color");
    static const CConfigValue<Config::STRING>  PLABELSHOW("plugin:hyprexpo:label_show");
    static const CConfigValue<Config::INTEGER> PLABELSIZE("plugin:hyprexpo:label_font_size");
    static const CConfigValue<Config::STRING>  PLABELFONT("plugin:hyprexpo:label_font_family");
    static const CConfigValue<Config::INTEGER> PLABELBOLD("plugin:hyprexpo:label_font_bold");
    static const CConfigValue<Config::INTEGER> PLABELITALIC("plugin:hyprexpo:label_font_italic");
    static const CConfigValue<Config::INTEGER> PLABELCOLDEFAULT("plugin:hyprexpo:label_color_default");
    static const CConfigValue<Config::INTEGER> PLABELCOLHOVER("plugin:hyprexpo:label_color_hover");
    static const CConfigValue<Config::INTEGER> PLABELCOLFOCUS("plugin:hyprexpo:label_color_focus");
    static const CConfigValue<Config::INTEGER> PLABELCOLCURRENT("plugin:hyprexpo:label_color_current");
    static const CConfigValue<Config::INTEGER> PWORKSPACENUMCOL("plugin:hyprexpo:workspace_number_color");
    static const CConfigValue<Config::INTEGER> PLABELBGENABLE("plugin:hyprexpo:label_bg_enable");
    static const CConfigValue<Config::INTEGER> PLABELBGCOLOR("plugin:hyprexpo:label_bg_color");
    static const CConfigValue<Config::INTEGER> PLABELBGROUND("plugin:hyprexpo:label_bg_rounding");
    static const CConfigValue<Config::INTEGER> PLABELBGPAD("plugin:hyprexpo:label_padding");
    static const CConfigValue<Config::INTEGER> PLABELPIXELSNAP("plugin:hyprexpo:label_pixel_snap");
    static const CConfigValue<Config::INTEGER> PBORDERWIDTH("plugin:hyprexpo:border_width");
    static const CConfigValue<Config::INTEGER> PBORDERCURRENT("plugin:hyprexpo:border_color_current");
    static const CConfigValue<Config::INTEGER> PBORDERHOVER("plugin:hyprexpo:border_color_hover");
    static const CConfigValue<Config::INTEGER> PBORDERFOCUS("plugin:hyprexpo:border_color_focus");

    const double                               outer          = *PGAPOUT * (closing ? (1.0 - size->getPercent()) : size->getPercent());
    Vector2D                                   tileRenderSize = (SIZE - Vector2D{GAPSIZE, GAPSIZE} * (SIDE_LENGTH - 1) - Vector2D{outer * 2, outer * 2}) / SIDE_LENGTH;

    clearWithColor(BG_COLOR.stripA());

    const int   baseRoundPx    = std::max(0, (int)std::lround((double)*PTILEROUND * pMonitor->m_scale));
    const int   hoverRoundPx   = *PTILEROUNDHOVER >= 0 ? std::max(0, (int)std::lround((double)*PTILEROUNDHOVER * pMonitor->m_scale)) : baseRoundPx;
    const int   focusRoundPx   = *PTILEROUNDFOCUS >= 0 ? std::max(0, (int)std::lround((double)*PTILEROUNDFOCUS * pMonitor->m_scale)) : baseRoundPx;
    const int   currentRoundPx = *PTILEROUNDCURRENT >= 0 ? std::max(0, (int)std::lround((double)*PTILEROUNDCURRENT * pMonitor->m_scale)) : baseRoundPx;
    const float roundingPower  = *PROUNDINGPOWER;

    auto        tileBoxFor = [&](size_t x, size_t y) {
        CBox box = {outer + x * tileRenderSize.x + x * GAPSIZE, outer + y * tileRenderSize.y + y * GAPSIZE, tileRenderSize.x, tileRenderSize.y};
        box.scale(pMonitor->m_scale).translate(pos->value());
        box.round();
        return box;
    };

    auto tileRoundFor = [&](int id, const CBox& box) {
        int round = baseRoundPx;
        if (id == kbFocusID)
            round = focusRoundPx;
        else if (id == openedID)
            round = currentRoundPx;
        else if (id == hoveredID)
            round = hoverRoundPx;

        return std::min(round, std::max(0, (int)std::floor(std::min(box.w, box.h) / 2.0)));
    };

    std::vector<CBox> tileBoxes(images.size());

    for (size_t y = 0; y < (size_t)SIDE_LENGTH; ++y) {
        for (size_t x = 0; x < (size_t)SIDE_LENGTH; ++x) {
            const int id     = x + y * SIDE_LENGTH;
            CBox      texbox = tileBoxFor(x, y);
            tileBoxes[id]    = texbox;

            CRegion damage{0, 0, INT16_MAX, INT16_MAX};
            auto&   image = images[id];
            Render::GL::g_pHyprOpenGL->renderTextureInternal(image.fb->getTexture(), texbox,
                                                             {.damage = &damage, .a = 1.0, .round = tileRoundFor(id, texbox), .roundingPower = roundingPower});
        }
    }

    const bool labelsEnabled          = *PLABELENABLE || showWorkspaceNumbers;
    const bool selectionLabelsEnabled = *PSELECTLABELENABLE;
    if (labelsEnabled || selectionLabelsEnabled) {
        const std::string tokenMapConfig  = *PLABELTOKENMAP;
        const auto        tokenMap        = tokenMapConfig.empty() ? std::vector<std::string>{} : splitCommaList(tokenMapConfig);
        const std::string selectMapConfig = *PSELECTLABELTOKENS;
        const auto        selectMap       = selectMapConfig.empty() ? std::vector<std::string>{} : splitCommaList(selectMapConfig);
        size_t            visible         = 0;

        auto drawLabel = [&](COverview::SWorkspaceImage::SLabelTexture& cache, const std::string& label, uint64_t color, const std::string& position, int offsetX, int offsetY,
                             const CBox& tileBox) {
            if (label.empty())
                return;

            const int fontSize = std::max(8, (int)*PLABELSIZE);
            auto      tex      = ensureLabelTexture(cache, label, color, fontSize, *PLABELFONT, *PLABELBOLD, *PLABELITALIC);
            if (!tex || tex->m_texID == 0 || cache.sizePx.x <= 0 || cache.sizePx.y <= 0)
                return;

            const Vector2D labelSize = cache.sizePx;
            CBox           labelBox  = {0, 0, labelSize.x, labelSize.y};
            const double   offX      = offsetX * pMonitor->m_scale;
            const double   offY      = offsetY * pMonitor->m_scale;

            if (position == "top-left") {
                labelBox.x = tileBox.x + offX;
                labelBox.y = tileBox.y + offY;
            } else if (position == "top-right") {
                labelBox.x = tileBox.x + tileBox.w - labelBox.w - offX;
                labelBox.y = tileBox.y + offY;
            } else if (position == "bottom-left") {
                labelBox.x = tileBox.x + offX;
                labelBox.y = tileBox.y + tileBox.h - labelBox.h - offY;
            } else if (position == "bottom-right") {
                labelBox.x = tileBox.x + tileBox.w - labelBox.w - offX;
                labelBox.y = tileBox.y + tileBox.h - labelBox.h - offY;
            } else {
                labelBox.x = tileBox.x + (tileBox.w - labelBox.w) / 2.0 + offX;
                labelBox.y = tileBox.y + (tileBox.h - labelBox.h) / 2.0 + offY;
            }

            if (*PLABELPIXELSNAP)
                labelBox.round();

            if (*PLABELBGENABLE) {
                const double pad = *PLABELBGPAD * pMonitor->m_scale;
                CBox         bg  = {labelBox.x - pad, labelBox.y - pad, labelBox.w + pad * 2, labelBox.h + pad * 2};
                if (*PLABELPIXELSNAP)
                    bg.round();
                Render::GL::g_pHyprOpenGL->renderRect(bg, CHyprColor(*PLABELBGCOLOR), {.round = std::max(0, (int)std::lround((double)*PLABELBGROUND * pMonitor->m_scale))});
            }

            Render::GL::g_pHyprOpenGL->renderTexture(tex, labelBox, {.a = 1.0});
        };

        for (size_t id = 0; id < images.size(); ++id) {
            auto& image = images[id];
            if (image.workspaceID == WORKSPACE_INVALID)
                continue;

            const bool        isHover   = (int)id == hoveredID;
            const bool        isFocus   = (int)id == kbFocusID;
            const bool        isCurrent = (int)id == openedID;

            const std::string showMode   = *PLABELSHOW;
            const bool        shouldShow = showWorkspaceNumbers || showMode == "always" || (showMode == "hover" && isHover) || (showMode == "focus" && isFocus) ||
                (showMode == "hover+focus" && (isHover || isFocus)) || (showMode == "current+focus" && (isCurrent || isFocus));
            const CBox& tileBox = tileBoxes[id];

            if (labelsEnabled && shouldShow && (showWorkspaceNumbers || showMode != "never")) {
                std::string label;
                const auto  labelMode = showWorkspaceNumbers ? std::string{"id"} : std::string{*PLABELMODE};
                if (labelMode == "index")
                    label = std::to_string(visible + 1);
                else if (labelMode == "token")
                    label = visible < tokenMap.size() ? tokenMap[visible] : fallbackTokenForVisibleIndex(visible);
                else
                    label = std::to_string(image.workspaceID);

                int      state = 0;
                uint64_t color = showWorkspaceNumbers ? *PWORKSPACENUMCOL : *PLABELCOLDEFAULT;
                if (!showWorkspaceNumbers && isFocus) {
                    state = 2;
                    color = *PLABELCOLFOCUS;
                } else if (!showWorkspaceNumbers && isCurrent) {
                    state = 3;
                    color = *PLABELCOLCURRENT;
                } else if (!showWorkspaceNumbers && isHover) {
                    state = 1;
                    color = *PLABELCOLHOVER;
                }

                drawLabel(image.labels[state], label, color, *PLABELPOS, *PLABELOFFX, *PLABELOFFY, tileBox);
            }

            if (selectionLabelsEnabled) {
                const std::string token = visible < selectMap.size() ? selectMap[visible] : "";
                drawLabel(image.selectionLabel, token, *PSELECTLABELCOL, *PSELECTLABELPOS, *PSELECTLABELOFFX, *PSELECTLABELOFFY, tileBox);
            }

            ++visible;
        }
    }

    auto drawBorder = [&](int id, uint64_t color) {
        if (id < 0 || id >= (int)images.size() || *PBORDERWIDTH <= 0 || CHyprColor(color).a <= 0.0)
            return;

        const CBox& box = tileBoxes[id];
        Render::GL::g_pHyprOpenGL->renderBorder(box, gradientFromColor(color),
                                                {.round = tileRoundFor(id, box), .roundingPower = roundingPower, .borderSize = std::max(1, (int)*PBORDERWIDTH)});
    };

    if (hoveredID != -1 && hoveredID != openedID && hoveredID != kbFocusID)
        drawBorder(hoveredID, *PBORDERHOVER);
    if (openedID != -1)
        drawBorder(openedID, *PBORDERCURRENT);
    if (kbFocusID != -1)
        drawBorder(kbFocusID, *PBORDERFOCUS);
    if (dragMoved && dragSourceID != -1)
        drawBorder(dragSourceID, *PBORDERFOCUS);

    if (dragWindow && isTileValid(dragSourceID)) {
        const auto WINDOWBOX = dragWindow->getWindowMainSurfaceBox();
        if (WINDOWBOX.w > 0 && WINDOWBOX.h > 0) {
            const CBox&  sourceBox = tileBoxes[dragSourceID];
            const double scaleX    = sourceBox.w / pMonitor->m_size.x;
            const double scaleY    = sourceBox.h / pMonitor->m_size.y;
            const int    round     = tileRoundFor(dragSourceID, sourceBox);
            const double minW      = std::min(sourceBox.w, 24.0 * pMonitor->m_scale);
            const double minH      = std::min(sourceBox.h, 24.0 * pMonitor->m_scale);

            CBox proxy = {
                lastMousePosLocal.x * pMonitor->m_scale - dragGrabOffset.x * scaleX,
                lastMousePosLocal.y * pMonitor->m_scale - dragGrabOffset.y * scaleY,
                std::clamp(WINDOWBOX.w * scaleX, minW, sourceBox.w),
                std::clamp(WINDOWBOX.h * scaleY, minH, sourceBox.h),
            };
            proxy.round();

            Render::GL::g_pHyprOpenGL->renderRect(proxy, CHyprColor{0.93F, 0.70F, 0.26F, dragMoved ? 0.24F : 0.14F}, {.round = round, .roundingPower = roundingPower});
            Render::GL::g_pHyprOpenGL->renderBorder(proxy, gradientFromColor(*PBORDERFOCUS),
                                                    {.round = round, .roundingPower = roundingPower, .borderSize = std::max(2, (int)*PBORDERWIDTH + 1)});
        }
    }
}

static float lerp(const float& from, const float& to, const float perc) {
    return (to - from) * perc + from;
}

static Vector2D lerp(const Vector2D& from, const Vector2D& to, const float perc) {
    return Vector2D{lerp(from.x, to.x, perc), lerp(from.y, to.y, perc)};
}

void COverview::setClosing(bool closing_) {
    closing = closing_;
}

void COverview::resetSwipe() {
    swipeWasCommenced = false;
}

void COverview::onSwipeUpdate(double delta) {
    m_isSwiping = true;

    static const CConfigValue<Config::INTEGER> PDISTANCE("plugin:hyprexpo:gesture_distance");

    const float                                PERC = closing ? std::clamp(delta / (double)*PDISTANCE, 0.0, 1.0) : 1.0 - std::clamp(delta / (double)*PDISTANCE, 0.0, 1.0);
    const auto                                 WORKSPACE_FOCUS_ID = closing && closeOnID != -1 ? closeOnID : openedID;

    Vector2D                                   tileSize = (pMonitor->m_size / SIDE_LENGTH);

    const auto                                 SIZEMAX = pMonitor->m_size * pMonitor->m_size / tileSize;
    const auto POSMAX = (-((pMonitor->m_size / (double)SIDE_LENGTH) * Vector2D{WORKSPACE_FOCUS_ID % SIDE_LENGTH, WORKSPACE_FOCUS_ID / SIDE_LENGTH}) * pMonitor->m_scale) *
        (pMonitor->m_size / tileSize);

    const auto SIZEMIN = pMonitor->m_size;
    const auto POSMIN  = Vector2D{0, 0};

    size->setValueAndWarp(lerp(SIZEMIN, SIZEMAX, PERC));
    pos->setValueAndWarp(lerp(POSMIN, POSMAX, PERC));
}

void COverview::onSwipeEnd() {
    if (closing || !m_isSwiping)
        return;

    const auto SIZEMIN = pMonitor->m_size;
    const auto SIZEMAX = pMonitor->m_size * pMonitor->m_size / (pMonitor->m_size / SIDE_LENGTH);
    const auto PERC    = (size->value() - SIZEMIN).x / (SIZEMAX - SIZEMIN).x;
    if (PERC > 0.5) {
        close();
        return;
    }
    *size = pMonitor->m_size;
    *pos  = {0, 0};

    size->setCallbackOnEnd([this](WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) { redrawAll(true); });

    swipeWasCommenced = false;
    m_isSwiping       = false;
}
