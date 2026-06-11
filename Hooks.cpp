#define WLR_USE_UNSTABLE

#include "Hooks.hpp"
#include "Globals.hpp"
#include "Overview.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <initializer_list>

static CFunctionHook* g_pRenderWorkspaceHook    = nullptr;
static CFunctionHook* g_pAddDamageHookA         = nullptr;
static CFunctionHook* g_pAddDamageHookB         = nullptr;
static CFunctionHook* g_pShouldRenderWindowHook = nullptr;

typedef void (*origRenderWorkspace)(void*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&, const CBox&);
typedef void (*origAddDamageA)(void*, const CBox&);
typedef void (*origAddDamageB)(void*, const pixman_region32_t*);
typedef bool (*origShouldRenderWindow)(void*, PHLWINDOW, PHLMONITOR);

static bool         g_renderingOverview = false;
static PHLMONITOR   g_livePreviewMonitor;
static PHLWORKSPACE g_livePreviewWorkspace;

static bool         livePreviewActiveForMonitor(PHLMONITOR pMonitor) {
    return g_livePreviewWorkspace && pMonitor && pMonitor == g_livePreviewMonitor;
}

static bool canConstrainWindowToLivePreviewWorkspace(PHLWINDOW pWindow) {
    if (!pWindow || !pWindow->m_workspace)
        return false;

    // Windows being unmapped can still be considered renderable by Hyprland for
    // fade-out paths. Leave those lifecycle cases to Hyprland's renderer instead
    // of treating them as stable live-preview workspace members.
    return pWindow->m_isMapped;
}

void setRenderingOverview(bool rendering) {
    g_renderingOverview = rendering;
}

void setLivePreviewWorkspace(PHLMONITOR monitor, PHLWORKSPACE workspace) {
    g_livePreviewMonitor   = monitor;
    g_livePreviewWorkspace = workspace;
}

void renderWorkspaceOriginal(PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& now, const CBox& geometry) {
    if (!g_pRenderWorkspaceHook)
        return;

    ((origRenderWorkspace)(g_pRenderWorkspaceHook->m_original))(g_pHyprRenderer.get(), pMonitor, pWorkspace, now, geometry);
}

static void hkRenderWorkspace(void* thisptr, PHLMONITOR pMonitor, PHLWORKSPACE pWorkspace, const Time::steady_tp& now, const CBox& geometry) {
    if (!g_pOverview || g_renderingOverview || g_pOverview->blockOverviewRendering || g_pOverview->pMonitor != pMonitor)
        ((origRenderWorkspace)(g_pRenderWorkspaceHook->m_original))(thisptr, pMonitor, pWorkspace, now, geometry);
    else
        g_pOverview->render();
}

static void hkAddDamageA(void* thisptr, const CBox& box) {
    const auto PMONITOR = (CMonitor*)thisptr;

    if (!g_pOverview || g_pOverview->pMonitor != PMONITOR->m_self || g_pOverview->blockDamageReporting) {
        ((origAddDamageA)g_pAddDamageHookA->m_original)(thisptr, box);
        return;
    }

    g_pOverview->onDamageReported();
}

static void hkAddDamageB(void* thisptr, const pixman_region32_t* rg) {
    const auto PMONITOR = (CMonitor*)thisptr;

    if (!g_pOverview || g_pOverview->pMonitor != PMONITOR->m_self || g_pOverview->blockDamageReporting) {
        ((origAddDamageB)g_pAddDamageHookB->m_original)(thisptr, rg);
        return;
    }

    g_pOverview->onDamageReported();
}

static bool hkShouldRenderWindow(void* thisptr, PHLWINDOW pWindow, PHLMONITOR pMonitor) {
    const bool result = ((origShouldRenderWindow)(g_pShouldRenderWindowHook->m_original))(thisptr, pWindow, pMonitor);

    if (!result || !livePreviewActiveForMonitor(pMonitor))
        return result;

    if (!canConstrainWindowToLivePreviewWorkspace(pWindow))
        return result;

    if (pWindow->m_pinned)
        return true;

    const auto PWINDOWWORKSPACE = pWindow->m_workspace;
    if (PWINDOWWORKSPACE == g_livePreviewWorkspace)
        return true;

    if (pMonitor->m_activeSpecialWorkspace && PWINDOWWORKSPACE == pMonitor->m_activeSpecialWorkspace)
        return true;

    return false;
}

static bool findHookTarget(const std::string& name, SFunctionMatch& target, std::string& error) {
    const auto FNS = HyprlandAPI::findFunctionsByName(PHANDLE, name);
    if (FNS.empty()) {
        error = "no fns for hook " + name;
        return false;
    }

    target = FNS[0];
    return true;
}

static bool findHookTarget(std::initializer_list<const char*> names, const std::string& label, SFunctionMatch& target, std::string& error) {
    for (const auto* name : names) {
        if (findHookTarget(name, target, error))
            return true;
    }

    error = "no fns for hook " + label;
    return false;
}

bool installHooks(std::string& error) {
    SFunctionMatch target;

    // These long strings are Itanium C++ ABI-mangled symbol names for the
    // Hyprland methods being hooked. They identify the exact overload and
    // signature; run them through c++filt to see the demangled method names.
    // Short names are kept as compatibility fallbacks where Hyprland can find
    // the hook target without the full linker symbol.
    if (!findHookTarget(
            {
                // Render::IHyprRenderer::renderWorkspace(...)
                "_ZN6Render13IHyprRenderer15renderWorkspaceEN9Hyprutils6Memory14CSharedPointerI8CMonitorEENS3_I10CWorkspaceEERKNSt6chrono10time_pointINS8_3_V212steady_clockENS8_"
                "8durationIlSt5ratioILl1ELl1000000000EEEEEERKNS1_4Math4CBoxE",
                "renderWorkspace",
            },
            "renderWorkspace", target, error))
        return false;
    g_pRenderWorkspaceHook = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkRenderWorkspace);

    if (!findHookTarget(
            {
                // CMonitor::addDamage(pixman_region32 const*)
                "_ZN8CMonitor9addDamageEPK15pixman_region32",
                "addDamageEPK15pixman_region32",
            },
            "addDamage(pixman_region32)", target, error))
        return false;
    g_pAddDamageHookB = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkAddDamageB);

    // CMonitor::addDamage(Hyprutils::Math::CBox const&)
    if (!findHookTarget("_ZN8CMonitor9addDamageERKN9Hyprutils4Math4CBoxE", target, error))
        return false;
    g_pAddDamageHookA = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkAddDamageA);

    if (!findHookTarget(
            {
                // Render::IHyprRenderer::shouldRenderWindow(PHLWINDOW, PHLMONITOR)
                "_ZN6Render13IHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CSharedPointerI7CWindowEENS3_I8CMonitorEE",
                "shouldRenderWindow",
            },
            "shouldRenderWindow", target, error))
        return false;
    g_pShouldRenderWindowHook = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkShouldRenderWindow);

    bool success = g_pRenderWorkspaceHook->hook();
    success      = success && g_pAddDamageHookA->hook();
    success      = success && g_pAddDamageHookB->hook();
    success      = success && g_pShouldRenderWindowHook->hook();

    if (!success) {
        error = "Failed initializing hooks";
        return false;
    }

    return true;
}
