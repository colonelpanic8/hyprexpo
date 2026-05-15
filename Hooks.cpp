#define WLR_USE_UNSTABLE

#include "Hooks.hpp"
#include "Globals.hpp"
#include "Overview.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/render/Renderer.hpp>

static CFunctionHook* g_pRenderWorkspaceHook = nullptr;
static CFunctionHook* g_pAddDamageHookA      = nullptr;
static CFunctionHook* g_pAddDamageHookB      = nullptr;

typedef void (*origRenderWorkspace)(void*, PHLMONITOR, PHLWORKSPACE, const Time::steady_tp&, const CBox&);
typedef void (*origAddDamageA)(void*, const CBox&);
typedef void (*origAddDamageB)(void*, const pixman_region32_t*);

static bool g_renderingOverview = false;

void        setRenderingOverview(bool rendering) {
    g_renderingOverview = rendering;
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

static bool findHookTarget(const std::string& name, SFunctionMatch& target, std::string& error) {
    const auto FNS = HyprlandAPI::findFunctionsByName(PHANDLE, name);
    if (FNS.empty()) {
        error = "no fns for hook " + name;
        return false;
    }

    target = FNS[0];
    return true;
}

bool installHooks(std::string& error) {
    SFunctionMatch target;

    if (!findHookTarget("renderWorkspace", target, error))
        return false;
    g_pRenderWorkspaceHook = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkRenderWorkspace);

    if (!findHookTarget("addDamageEPK15pixman_region32", target, error))
        return false;
    g_pAddDamageHookB = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkAddDamageB);

    if (!findHookTarget("_ZN8CMonitor9addDamageERKN9Hyprutils4Math4CBoxE", target, error))
        return false;
    g_pAddDamageHookA = HyprlandAPI::createFunctionHook(PHANDLE, target.address, (void*)hkAddDamageA);

    bool success = g_pRenderWorkspaceHook->hook();
    success      = success && g_pAddDamageHookA->hook();
    success      = success && g_pAddDamageHookB->hook();

    if (!success) {
        error = "Failed initializing hooks";
        return false;
    }

    return true;
}
