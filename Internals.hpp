#pragma once

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

namespace Internals {

    std::string                lowerCopy(std::string value);
    std::string                fallbackTokenForVisibleIndex(size_t visible);
    std::optional<size_t>      fallbackTokenToVisibleIndex(const std::string& token);
    uint32_t                   framebufferFormatWithAlpha(uint32_t drmFormat);
    void                       clearWithColor(const CHyprColor& color);
    Config::CGradientValueData gradientFromColor(uint64_t color);
    bool                       windowVisibleOnWorkspace(const PHLWINDOW& window, const PHLWORKSPACE& workspace);
    void                       settleWorkspaceMoveAnimation(const PHLWINDOW& window);
    void                       ensureFramebuffer(COverview::SWorkspaceImage& image, const CBox& monbox, uint32_t drmFormat);
    void                       damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr);

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

    SWorkspacePreviewState           applyWorkspacePreviewState(const PHLWORKSPACE& workspace);
    void                             restoreWorkspacePreviewState(const PHLWORKSPACE& workspace, const SWorkspacePreviewState& state);
    std::vector<SWindowPreviewState> applyWorkspaceWindowPreviewState(const PHLWORKSPACE& workspace);
    void                             restoreWorkspaceWindowPreviewState(const std::vector<SWindowPreviewState>& states);
    void                             recalculateWorkspaceForPreview(PHLMONITOR monitor, const PHLWORKSPACE& workspace);

}
