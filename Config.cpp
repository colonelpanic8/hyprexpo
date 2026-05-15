#include "Config.hpp"
#include "Globals.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>

static bool addConfigValue(SP<Config::Values::IValue> value) {
    const auto RET = Config::mgr()->registerPluginValue(PHANDLE, value);
    if (!RET) {
        Log::logger->log(Log::ERR, "[hyprexpo] failed to register plugin value \"{}\": {}", value->name(), RET.error());
        return false;
    }

    return true;
}

void registerConfigValues() {
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:columns", "columns", 3));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:gap_size", "gap size", 5));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:gap_size_outer", "outer gap size", 0));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:bg_col", "background color", 0xFF111111));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:workspace_method", "workspace method", "center current"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:skip_empty", "skip empty workspaces", 0));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:max_workspace", "maximum generated workspace", 0));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:show_workspace_numbers", "show workspace numbers", 0));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:workspace_number_color", "workspace number color", 0xFFFFFFFF));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:gesture_distance", "gesture distance", 200));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:cancel_key", "cancel key", "escape"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:tile_rounding", "tile rounding", 0));
    addConfigValue(makeShared<Config::Values::CFloatValue>("plugin:hyprexpo:tile_rounding_power", "tile rounding power", 2.F));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:tile_rounding_hover", "hover tile rounding override", -1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:tile_rounding_focus", "focused tile rounding override", -1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:tile_rounding_current", "current tile rounding override", -1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:border_width", "highlight border width", 0));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:border_color_current", "current tile border color", 0xFF66CCFF));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:border_color_hover", "hovered tile border color", 0xFFAABBCC));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:border_color_focus", "keyboard-focused tile border color", 0xFFFFCC66));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_enable", "show configurable labels", 0));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:label_text_mode", "label text mode", "token"));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:label_token_map", "label token overrides", ""));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:label_position", "label position", "top-left"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_offset_x", "label x offset", 6));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_offset_y", "label y offset", 6));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:selection_label_enable", "show selection labels", 0));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:selection_label_token_map", "selection label tokens", "a,s,d,f,g,q,w,e,r,t,z,x,c,v,b"));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:selection_label_position", "selection label position", "top-right"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:selection_label_offset_x", "selection label x offset", 6));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:selection_label_offset_y", "selection label y offset", 6));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:selection_label_color", "selection label color", 0xFFFFCC66));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:label_show", "label visibility mode", "always"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_font_size", "label font size", 16));
    addConfigValue(makeShared<Config::Values::CStringValue>("plugin:hyprexpo:label_font_family", "label font family", "Sans"));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_font_bold", "label font bold", 1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_font_italic", "label font italic", 0));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:label_color_default", "default label color", 0xFFFFFFFF));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:label_color_hover", "hover label color", 0xFFEEEEEE));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:label_color_focus", "keyboard-focused label color", 0xFFFFCC66));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:label_color_current", "current label color", 0xFF66CCFF));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_bg_enable", "label background", 1));
    addConfigValue(makeShared<Config::Values::CColorValue>("plugin:hyprexpo:label_bg_color", "label background color", 0x88000000));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_bg_rounding", "label background rounding", 8));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_padding", "label background padding", 6));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:label_pixel_snap", "snap label pixels", 1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:keynav_wrap_h", "keyboard navigation horizontal wrap", 1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:keynav_wrap_v", "keyboard navigation vertical wrap", 1));
    addConfigValue(makeShared<Config::Values::CIntValue>("plugin:hyprexpo:keynav_reading_order", "keyboard navigation reading order", 0));
}
