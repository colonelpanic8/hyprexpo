#define WLR_USE_UNSTABLE

#include "WorkspaceLayout.hpp"

#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/helpers/varlist/VarList.hpp>

#include <string_view>

using namespace Hyprutils::String;

static std::string trimCopy(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\n\r");
    if (first == std::string_view::npos)
        return "";

    const auto last = value.find_last_not_of(" \t\n\r");
    return std::string{value.substr(first, last - first + 1)};
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> entries;
    std::string_view         rest = value;

    while (true) {
        const auto comma = rest.find(',');
        entries.push_back(trimCopy(rest.substr(0, comma)));
        if (comma == std::string_view::npos)
            break;
        rest.remove_prefix(comma + 1);
    }

    return entries;
}

std::pair<bool, int> workspaceMethodForMonitor(PHLMONITOR monitor, const std::string& config) {
    std::string methodConfig;
    std::string fallback;

    for (const auto& entry : splitCommaList(config)) {
        if (entry.empty())
            continue;

        CVarList tokens{entry, 0, 's', true};
        if (tokens.size() == 3 && std::string{tokens[0]} == monitor->m_name) {
            methodConfig = std::string{tokens[1]} + " " + std::string{tokens[2]};
            break;
        }

        if (tokens.size() == 2 && fallback.empty())
            fallback = entry;
    }

    if (methodConfig.empty())
        methodConfig = fallback.empty() ? config : fallback;

    bool     methodCenter  = true;
    int      methodStartID = monitor->activeWorkspaceID();

    CVarList method{methodConfig, 0, 's', true};
    if (method.size() < 2) {
        Log::logger->log(Log::ERR, "[hyprexpo] invalid workspace_method for monitor {}: {}", monitor->m_name, methodConfig);
        return {methodCenter, methodStartID};
    }

    methodCenter  = method[0] == "center";
    methodStartID = getWorkspaceIDNameFromString(method[1]).id;
    if (methodStartID == WORKSPACE_INVALID)
        methodStartID = monitor->activeWorkspaceID();

    return {methodCenter, methodStartID};
}
