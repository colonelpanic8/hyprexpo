#include "Internals.hpp"

using namespace Hyprutils::String;
using namespace Internals;

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
            image.pWorkspace        = PWORKSPACE;
            const auto PREVIOUSWS    = activateWorkspaceForPreview(PMONITOR, PWORKSPACE);
            const auto PREVIEWSTATE  = applyWorkspacePreviewState(PWORKSPACE);

            if (PWORKSPACE == startedOn)
                PMONITOR->m_activeSpecialWorkspace = openSpecial;

            g_pHyprRenderer->renderWorkspace(PMONITOR, PWORKSPACE, Time::steadyNow(), monbox);

            restoreWorkspacePreviewState(PWORKSPACE, PREVIEWSTATE);
            restoreActiveWorkspaceAfterPreview(PMONITOR, PREVIOUSWS);
            startedOn->m_visible = false;

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
