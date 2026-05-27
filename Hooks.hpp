#pragma once

#define WLR_USE_UNSTABLE

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprutils/math/Box.hpp>

#include <string>

bool installHooks(std::string& error);
void setRenderingOverview(bool rendering);
void setLivePreviewWorkspace(PHLMONITOR monitor, PHLWORKSPACE workspace);
void renderWorkspaceOriginal(PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp& now, const CBox& geometry);
