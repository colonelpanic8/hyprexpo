#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <string>
#include <utility>
#include <vector>

std::vector<std::string> splitCommaList(const std::string& value);
std::pair<bool, int>     workspaceMethodForMonitor(PHLMONITOR monitor, const std::string& config);
