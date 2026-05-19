#include "Internals.hpp"

namespace Internals {

    std::string lowerCopy(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return std::tolower(c); });
        return value;
    }

    std::string fallbackTokenForVisibleIndex(size_t visible) {
        if (visible < 9)
            return std::to_string(visible + 1);
        if (visible == 9)
            return "0";
        if (visible < 36)
            return std::string(1, (char)('a' + visible - 10));

        return "";
    }

    std::optional<size_t> fallbackTokenToVisibleIndex(const std::string& token) {
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

    uint32_t framebufferFormatWithAlpha(uint32_t drmFormat) {
        const auto alphaFormat = NFormatUtils::alphaFormat(drmFormat);
        return alphaFormat == 0 ? DRM_FORMAT_ABGR8888 : alphaFormat;
    }

    void clearWithColor(const CHyprColor& color) {
        glClearColor(color.r, color.g, color.b, color.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    Config::CGradientValueData gradientFromColor(uint64_t color) {
        Config::CGradientValueData grad{CHyprColor(color)};
        grad.updateColorsOk();
        return grad;
    }

    bool windowVisibleOnWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace) {
        return window && workspace && window->m_workspace == workspace && window->m_isMapped && !window->isHidden() && !window->m_pinned;
    }

    void settleWorkspaceMoveAnimation(const PHLWINDOW& window) {
        if (!window)
            return;

        window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->resetAllCallbacks();
        window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE)->setValueAndWarp(1.F);
        *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_TO_WORKSPACE) = 1.F;
        window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE)->setValueAndWarp(1.F);
        *window->alpha(Desktop::View::WINDOW_ALPHA_MOVE_FROM_WORKSPACE) = 1.F;
        window->m_monitorMovedFrom                                      = -1;
    }

    void ensureFramebuffer(COverview::SWorkspaceImage& image, const CBox& monbox, uint32_t drmFormat) {
        if (!image.fb)
            image.fb = g_pHyprRenderer->createFB("hyprexpo");

        if (image.fb->m_size != monbox.size()) {
            image.fb->release();
            image.fb->alloc(monbox.w, monbox.h, drmFormat);
        }
    }

    void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr) {
        g_pOverview->damage();
    }

    SWorkspacePreviewState applyWorkspacePreviewState(const PHLWORKSPACE& workspace) {
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

    void restoreWorkspacePreviewState(const PHLWORKSPACE& workspace, const SWorkspacePreviewState& state) {
        if (!workspace)
            return;

        workspace->m_visible        = state.visible;
        workspace->m_forceRendering = state.forceRendering;
        workspace->m_alpha->setValueAndWarp(state.alphaValue);
        *workspace->m_alpha = state.alphaGoal;
        workspace->m_renderOffset->setValueAndWarp(state.offsetValue);
        *workspace->m_renderOffset = state.offsetGoal;
    }

    PHLWORKSPACE activateWorkspaceForPreview(PHLMONITOR monitor, const PHLWORKSPACE& workspace) {
        if (!monitor)
            return nullptr;

        const auto PREVIOUSWORKSPACE = monitor->m_activeWorkspace;

        if (!workspace)
            return PREVIOUSWORKSPACE;

        if (PREVIOUSWORKSPACE != workspace)
            monitor->changeWorkspace(workspace, true, true, true);

        if (g_layoutManager)
            g_layoutManager->recalculateMonitor(monitor);

        return PREVIOUSWORKSPACE;
    }

    void restoreActiveWorkspaceAfterPreview(PHLMONITOR monitor, const PHLWORKSPACE& workspace) {
        if (!monitor || !workspace)
            return;

        if (monitor->m_activeWorkspace != workspace)
            monitor->changeWorkspace(workspace, true, true, true);

        if (g_layoutManager)
            g_layoutManager->recalculateMonitor(monitor);
    }

}
