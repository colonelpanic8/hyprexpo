#pragma once

#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>

void            registerDispatchers();
bool            shouldCancelOverview(const IKeyboard::SKeyEvent& event);
bool            shouldSelectWorkspaceFromKey(const IKeyboard::SKeyEvent& event);
SDispatchResult onExpoDispatcher(std::string arg);
