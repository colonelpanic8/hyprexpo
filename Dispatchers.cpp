#define WLR_USE_UNSTABLE

#include "Dispatchers.hpp"
#include "Globals.hpp"
#include "Hooks.hpp"
#include "Overview.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/SeatManager.hpp>

#include <algorithm>
#include <cctype>
#include <lua.hpp>
#include <optional>
#include <string_view>
#include <xkbcommon/xkbcommon.h>

static PHLWINDOW windowToBringFromWorkspace(const PHLWORKSPACE& workspace) {
    if (!workspace)
        return nullptr;

    for (auto it = g_pCompositor->m_windows.rbegin(); it != g_pCompositor->m_windows.rend(); ++it) {
        const auto& w = *it;
        if (!w || w->m_workspace != workspace || !w->m_isMapped || w->isHidden())
            continue;

        return w;
    }

    return nullptr;
}

static SDispatchResult bringWindowFromWorkspace(int64_t sourceWorkspaceID) {
    if (sourceWorkspaceID == WORKSPACE_INVALID)
        return {.success = false, .error = "selected workspace is empty"};

    const auto FOCUSSTATE = Desktop::focusState();
    const auto MONITOR    = FOCUSSTATE->monitor();
    if (!MONITOR || !MONITOR->m_activeWorkspace)
        return {.success = false, .error = "no active monitor/workspace"};

    if (sourceWorkspaceID == MONITOR->activeWorkspaceID())
        return {};

    const auto SOURCEWORKSPACE = g_pCompositor->getWorkspaceByID(sourceWorkspaceID);
    if (!SOURCEWORKSPACE)
        return {.success = false, .error = "selected workspace is not open"};

    const auto WINDOW = windowToBringFromWorkspace(SOURCEWORKSPACE);
    if (!WINDOW)
        return {.success = false, .error = "selected workspace has no mapped windows"};

    g_pCompositor->moveWindowToWorkspaceSafe(WINDOW, MONITOR->m_activeWorkspace);
    FOCUSSTATE->fullWindowFocus(WINDOW, Desktop::FOCUS_REASON_KEYBIND);
    g_pCompositor->warpCursorTo(WINDOW->middle());
    return {};
}

static bool isSingleDigitWorkspaceArg(const std::string& arg) {
    return arg.size() == 1 && arg[0] >= '1' && arg[0] <= '9';
}

static std::string trimCopy(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos)
        return "";

    const auto last = value.find_last_not_of(" \t\n\r");
    return std::string{value.substr(first, last - first + 1)};
}

static std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}

static bool isCancelKeyDisabled(const std::string& keyName) {
    const auto KEY = lower(keyName);
    return KEY.empty() || KEY == "none" || KEY == "disabled" || KEY == "disable" || KEY == "off";
}

static bool keyNameMatchesKeysym(const std::string& keyName, xkb_keysym_t keysym) {
    if (keyName.empty())
        return false;

    const auto CONFIGUREDKEYSYM = xkb_keysym_from_name(keyName.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (CONFIGUREDKEYSYM == XKB_KEY_NoSymbol)
        return false;

    return xkb_keysym_to_lower(keysym) == xkb_keysym_to_lower(CONFIGUREDKEYSYM);
}

static bool matchesCancelKey(xkb_keysym_t keysym) {
    static const CConfigValue<Config::STRING> PCANCELKEY("plugin:hyprexpo:cancel_key");

    const std::string                         keyConfigString = *PCANCELKEY;
    std::string_view                          keyConfig       = keyConfigString;
    while (true) {
        const auto COMMA   = keyConfig.find(',');
        const auto KEYNAME = trimCopy(keyConfig.substr(0, COMMA));

        if (isCancelKeyDisabled(KEYNAME))
            return false;

        if (keyNameMatchesKeysym(KEYNAME, keysym))
            return true;

        if (COMMA == std::string_view::npos)
            return false;

        keyConfig.remove_prefix(COMMA + 1);
    }
}

bool shouldCancelOverview(const IKeyboard::SKeyEvent& event) {
    if (!g_pOverview || event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return false;

    const auto KEYCODE  = event.keycode + 8;
    const auto KEYBOARD = g_pSeatManager->m_keyboard.lock();

    if (KEYBOARD && KEYBOARD->m_xkbState && matchesCancelKey(xkb_state_key_get_one_sym(KEYBOARD->m_xkbState, KEYCODE)))
        return true;

    if (KEYBOARD && KEYBOARD->m_xkbSymState && matchesCancelKey(xkb_state_key_get_one_sym(KEYBOARD->m_xkbSymState, KEYCODE)))
        return true;

    return false;
}

static SDispatchResult changeToSingleDigitWorkspace(const std::string& arg) {
    const auto WORKSPACEID = arg[0] - '0';

    if (g_pOverview) {
        if (g_pOverview->selectWorkspaceByID(WORKSPACEID)) {
            g_pOverview->close();
            return {};
        }

        g_pOverview->close(false);
    }

    Config::Actions::changeWorkspace(arg);
    return {};
}

static std::string workspaceArgForKeysym(xkb_keysym_t keysym) {
    switch (keysym) {
        case XKB_KEY_1:
        case XKB_KEY_KP_1: return "1";
        case XKB_KEY_2:
        case XKB_KEY_KP_2: return "2";
        case XKB_KEY_3:
        case XKB_KEY_KP_3: return "3";
        case XKB_KEY_4:
        case XKB_KEY_KP_4: return "4";
        case XKB_KEY_5:
        case XKB_KEY_KP_5: return "5";
        case XKB_KEY_6:
        case XKB_KEY_KP_6: return "6";
        case XKB_KEY_7:
        case XKB_KEY_KP_7: return "7";
        case XKB_KEY_8:
        case XKB_KEY_KP_8: return "8";
        case XKB_KEY_9:
        case XKB_KEY_KP_9: return "9";
        default: return "";
    }
}

static std::string workspaceArgForKeyEvent(const IKeyboard::SKeyEvent& event) {
    if (!g_pOverview || event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return "";

    const auto KEYCODE  = event.keycode + 8;
    const auto KEYBOARD = g_pSeatManager->m_keyboard.lock();

    if (KEYBOARD && KEYBOARD->m_xkbState) {
        const auto ARG = workspaceArgForKeysym(xkb_state_key_get_one_sym(KEYBOARD->m_xkbState, KEYCODE));
        if (!ARG.empty())
            return ARG;
    }

    if (KEYBOARD && KEYBOARD->m_xkbSymState) {
        const auto ARG = workspaceArgForKeysym(xkb_state_key_get_one_sym(KEYBOARD->m_xkbSymState, KEYCODE));
        if (!ARG.empty())
            return ARG;
    }

    return "";
}

bool shouldSelectWorkspaceFromKey(const IKeyboard::SKeyEvent& event) {
    if (g_pOverview && g_pOverview->m_isSwiping)
        return false;

    const auto ARG = workspaceArgForKeyEvent(event);
    if (ARG.empty())
        return false;

    return changeToSingleDigitWorkspace(ARG).success;
}

static std::optional<size_t> tokenToVisibleIndex(const std::string& token) {
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

SDispatchResult onExpoDispatcher(std::string arg) {
    if (g_pOverview && g_pOverview->m_isSwiping)
        return {.success = false, .error = "already swiping"};

    if (isSingleDigitWorkspaceArg(arg))
        return changeToSingleDigitWorkspace(arg);

    if (arg == "select") {
        if (g_pOverview) {
            g_pOverview->selectHoveredWorkspace();
            g_pOverview->close();
        }
        return {};
    }
    if (arg == "bring") {
        if (g_pOverview) {
            g_pOverview->selectHoveredWorkspace();
            const auto BRINGRESULT = bringWindowFromWorkspace(g_pOverview->selectedWorkspaceID());
            g_pOverview->close(false);
            return BRINGRESULT;
        }
        return {};
    }
    if (arg == "toggle") {
        if (g_pOverview)
            g_pOverview->close();
        else {
            setRenderingOverview(true);
            g_pOverview = std::make_unique<COverview>(Desktop::focusState()->monitor()->m_activeWorkspace);
            setRenderingOverview(false);
        }
        return {};
    }
    if (arg == "cancel") {
        if (g_pOverview)
            g_pOverview->close(false);
        return {};
    }

    if (arg == "off" || arg == "close" || arg == "disable") {
        if (g_pOverview)
            g_pOverview->close();
        return {};
    }

    if (g_pOverview)
        return {};

    setRenderingOverview(true);
    g_pOverview = std::make_unique<COverview>(Desktop::focusState()->monitor()->m_activeWorkspace);
    setRenderingOverview(false);
    return {};
}

static int luaExpo(lua_State* L) {
    const auto RESULT = onExpoDispatcher(luaL_optstring(L, 1, "toggle"));
    if (!RESULT.success)
        return luaL_error(L, "%s", RESULT.error.c_str());
    return 0;
}

static SDispatchResult onKbFocusDispatcher(std::string arg) {
    if (!g_pOverview)
        return {};

    arg = trimCopy(arg);
    if (arg == "left")
        g_pOverview->moveKeyboardFocus(-1, 0);
    else if (arg == "right")
        g_pOverview->moveKeyboardFocus(1, 0);
    else if (arg == "up")
        g_pOverview->moveKeyboardFocus(0, -1);
    else if (arg == "down")
        g_pOverview->moveKeyboardFocus(0, 1);
    else
        return {.success = false, .error = "invalid arg. expected left, right, up, or down"};

    return {};
}

static SDispatchResult onKbConfirmDispatcher(std::string) {
    if (g_pOverview)
        g_pOverview->confirmKeyboardFocus();

    return {};
}

static SDispatchResult onKbSelectIndexDispatcher(std::string arg) {
    if (!g_pOverview)
        return {};

    arg = trimCopy(arg);

    size_t index = 0;
    try {
        index = std::stoull(arg);
    } catch (...) { return {.success = false, .error = "invalid index"}; }

    if (index == 0)
        return {.success = false, .error = "index is 1-based"};

    if (!g_pOverview->selectVisibleIndex(index - 1))
        return {.success = false, .error = "no visible workspace for index"};

    g_pOverview->close();
    return {};
}

static SDispatchResult onKbSelectTokenDispatcher(std::string arg) {
    if (!g_pOverview)
        return {};

    arg              = trimCopy(arg);
    const auto index = tokenToVisibleIndex(arg);
    if (!index)
        return {.success = false, .error = "invalid token. expected 1-9, 0, or a-z"};

    if (!g_pOverview->selectVisibleIndex(*index))
        return {.success = false, .error = "no visible workspace for token"};

    g_pOverview->close();
    return {};
}

void registerDispatchers() {
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprexpo:expo", ::onExpoDispatcher);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprexpo:kb_focus", ::onKbFocusDispatcher);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprexpo:kb_confirm", ::onKbConfirmDispatcher);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprexpo:kb_select", ::onKbSelectTokenDispatcher);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprexpo:kb_selecti", ::onKbSelectIndexDispatcher);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprexpo", "expo", ::luaExpo);
}
