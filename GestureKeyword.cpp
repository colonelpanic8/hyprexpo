#define WLR_USE_UNSTABLE

#include "GestureKeyword.hpp"
#include "ExpoGesture.hpp"

#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/input/trackpad/GestureTypes.hpp>
#include <hyprland/src/managers/input/trackpad/TrackpadGestures.hpp>
#include <hyprutils/string/ConstVarList.hpp>

#include <algorithm>
#include <format>

using namespace Hyprutils::String;

const std::string KEYWORD_EXPO_GESTURE = "hyprexpo-gesture";

static bool       g_unloading = false;

void              setGestureKeywordUnloading(bool unloading) {
    g_unloading = unloading;
}

Hyprlang::CParseResult expoGestureKeyword(const char* LHS, const char* RHS) {
    Hyprlang::CParseResult result;

    if (g_unloading)
        return result;

    CConstVarList             data(RHS);

    size_t                    fingerCount = 0;
    eTrackpadGestureDirection direction   = TRACKPAD_GESTURE_DIR_NONE;

    try {
        fingerCount = std::stoul(std::string{data[0]});
    } catch (...) {
        result.setError(std::format("Invalid value {} for finger count", data[0]).c_str());
        return result;
    }

    if (fingerCount <= 1 || fingerCount >= 10) {
        result.setError(std::format("Invalid value {} for finger count", data[0]).c_str());
        return result;
    }

    direction = g_pTrackpadGestures->dirForString(data[1]);

    if (direction == TRACKPAD_GESTURE_DIR_NONE) {
        result.setError(std::format("Invalid direction: {}", data[1]).c_str());
        return result;
    }

    int      startDataIdx   = 2;
    uint32_t modMask        = 0;
    float    deltaScale     = 1.F;
    bool     disableInhibit = false;

    for (const auto arg : std::string(LHS).substr(KEYWORD_EXPO_GESTURE.size())) {
        switch (arg) {
            case 'p': disableInhibit = true; break;
            default: result.setError("hyprexpo-gesture: invalid flag"); return result;
        }
    }

    while (true) {
        if (data[startDataIdx].starts_with("mod:")) {
            modMask = g_pKeybindManager->stringToModMask(std::string{data[startDataIdx].substr(4)});
            startDataIdx++;
            continue;
        } else if (data[startDataIdx].starts_with("scale:")) {
            try {
                deltaScale = std::clamp(std::stof(std::string{data[startDataIdx].substr(6)}), 0.1F, 10.F);
                startDataIdx++;
                continue;
            } catch (...) {
                result.setError(std::format("Invalid delta scale: {}", std::string{data[startDataIdx].substr(6)}).c_str());
                return result;
            }
        }

        break;
    }

    std::expected<void, std::string> resultFromGesture;

    if (data[startDataIdx] == "expo")
        resultFromGesture = g_pTrackpadGestures->addGesture(makeUnique<CExpoGesture>(), fingerCount, direction, modMask, deltaScale, disableInhibit);
    else if (data[startDataIdx] == "unset")
        resultFromGesture = g_pTrackpadGestures->removeGesture(fingerCount, direction, modMask, deltaScale, disableInhibit);
    else {
        result.setError(std::format("Invalid gesture: {}", data[startDataIdx]).c_str());
        return result;
    }

    if (!resultFromGesture) {
        result.setError(resultFromGesture.error().c_str());
        return result;
    }

    return result;
}
