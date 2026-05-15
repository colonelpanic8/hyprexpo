#define WLR_USE_UNSTABLE

#include "Config.hpp"
#include "Dispatchers.hpp"
#include "GestureKeyword.hpp"
#include "Globals.hpp"
#include "Hooks.hpp"
#include "Overview.hpp"
#include "OverviewPassElement.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/legacy/ConfigManager.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/render/Renderer.hpp>

static void failNotif(const std::string& reason) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprexpo] Failure in initialization: " + reason, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
}

static void addLegacyConfigKeyword(const std::string& name, Hyprlang::PCONFIGHANDLERFUNC function, Hyprlang::SHandlerOptions options = {}) {
    if (Config::mgr()->type() != Config::CONFIG_LEGACY) {
        Log::logger->log(Log::WARN, "[hyprexpo] config keyword {} is only supported by the legacy config backend", name);
        return;
    }

    static_cast<Config::Legacy::CConfigManager*>(Config::mgr().get())->addPluginKeyword(PHANDLE, name, function, options);
}

// Do NOT change this function.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
#endif
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        failNotif("Version mismatch (headers ver is not equal to running hyprland ver)");
        throw std::runtime_error("[he] Version mismatch");
    }

    std::string hookError;
    if (!installHooks(hookError)) {
        failNotif(hookError);
        throw std::runtime_error("[he] " + hookError);
    }

    static auto P = Event::bus()->m_events.render.pre.listen([](PHLMONITOR) {
        if (!g_pOverview)
            return;
        g_pOverview->onPreRender();
    });

    static auto PKEY = Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo& info) {
        if (shouldSelectWorkspaceFromKey(event)) {
            info.cancelled = true;
            return;
        }

        if (!shouldCancelOverview(event))
            return;

        info.cancelled = true;
        g_pOverview->close(false);
    });

    registerDispatchers();
    addLegacyConfigKeyword(KEYWORD_EXPO_GESTURE, ::expoGestureKeyword, {true});
    registerConfigValues();

    return {"hyprexpo", "A plugin for an overview", "Vaxry", "1.0"};
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

APICALL EXPORT void PLUGIN_EXIT() {
    g_pHyprRenderer->m_renderPass.removeAllOfType("COverviewPassElement");

    setGestureKeywordUnloading(true);

    Config::mgr()->reload(); // we need to reload now to clear all the gestures
}
