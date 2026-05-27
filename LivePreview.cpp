#include "Hooks.hpp"
#include "Internals.hpp"

using namespace Internals;

int COverview::initializeLivePreviews() {
    int      currentid = 0;

    Vector2D tileRenderSize = (pMonitor->m_size - Vector2D{GAP_WIDTH * pMonitor->m_scale, GAP_WIDTH * pMonitor->m_scale} * (SIDE_LENGTH - 1)) / SIDE_LENGTH;

    for (size_t i = 0; i < (size_t)(SIDE_LENGTH * SIDE_LENGTH); ++i) {
        auto& image      = images[i];
        image.pWorkspace = g_pCompositor->getWorkspaceByID(image.workspaceID);

        if (image.pWorkspace == startedOn)
            currentid = i;

        image.box = {(i % SIDE_LENGTH) * tileRenderSize.x + (i % SIDE_LENGTH) * GAP_WIDTH, (i / SIDE_LENGTH) * tileRenderSize.y + (i / SIDE_LENGTH) * GAP_WIDTH, tileRenderSize.x,
                     tileRenderSize.y};
    }

    return currentid;
}

void COverview::renderLivePreviewTiles(const std::vector<CBox>& tileBoxes) {
    if (!pMonitor)
        return;

    const auto PMONITOR = pMonitor.lock();
    const auto TIME     = Time::steadyNow();
    const auto OLDWS    = PMONITOR->m_activeWorkspace;
    const auto OLDSPEC  = PMONITOR->m_activeSpecialWorkspace;

    g_pHyprRenderer->m_bBlockSurfaceFeedback = true;

    auto renderTile = [&](size_t id) {
        if (!isTileValid(id) || id >= tileBoxes.size())
            return;

        auto& image = images[id];
        auto  ws    = image.pWorkspace ? image.pWorkspace : g_pCompositor->getWorkspaceByID(image.workspaceID);
        if (!ws)
            return;

        image.pWorkspace = ws;

        const CBox& tileBox = tileBoxes[id];
        if (tileBox.w <= 0.01 || tileBox.h <= 0.01)
            return;

        const double sourceWidth = std::max(1.0, (double)PMONITOR->m_pixelSize.x);
        const double scale       = tileBox.w / sourceWidth;
        CBox         renderBox{{tileBox.pos() / scale}, tileBox.size()};
        if (PMONITOR->m_transform % 2 == 1)
            std::swap(renderBox.w, renderBox.h);

        if (OLDSPEC && ws != startedOn)
            PMONITOR->m_activeSpecialWorkspace.reset();
        else if (OLDSPEC && ws == startedOn)
            PMONITOR->m_activeSpecialWorkspace = OLDSPEC;

        const auto PREVIOUSWS   = activateWorkspaceForPreview(PMONITOR, ws);
        const auto PREVIEWSTATE = applyWorkspacePreviewState(ws);

        renderWorkspaceOriginal(PMONITOR, ws, TIME, renderBox);

        restoreWorkspacePreviewState(ws, PREVIEWSTATE);
        restoreActiveWorkspaceAfterPreview(PMONITOR, PREVIOUSWS);

        if (OLDSPEC && ws == startedOn)
            PMONITOR->m_activeSpecialWorkspace.reset();
    };

    for (size_t id = 0; id < images.size(); ++id) {
        const auto ws = images[id].pWorkspace ? images[id].pWorkspace : g_pCompositor->getWorkspaceByID(images[id].workspaceID);
        if (ws == OLDWS)
            continue;

        renderTile(id);
    }

    for (size_t id = 0; id < images.size(); ++id) {
        const auto ws = images[id].pWorkspace ? images[id].pWorkspace : g_pCompositor->getWorkspaceByID(images[id].workspaceID);
        if (ws != OLDWS)
            continue;

        renderTile(id);
        break;
    }

    g_pHyprRenderer->m_bBlockSurfaceFeedback = false;
    PMONITOR->m_activeSpecialWorkspace       = OLDSPEC;
    PMONITOR->m_activeWorkspace              = OLDWS;
    if (startedOn) {
        startedOn->m_visible = true;
        g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
    }
}
