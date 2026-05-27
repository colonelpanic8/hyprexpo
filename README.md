# HyprExpo
HyprExpo is an overview plugin like Gnome, KDE or wf.
  
![HyprExpo](https://github.com/user-attachments/assets/e89df9d2-9800-4268-9929-239ad9bc3a54)
  
## Installation

```bash
hyprpm add https://github.com/colonelpanic8/hyprexpo
hyprpm enable hyprexpo
hyprpm reload
```

## Config
A great start to configure this plugin would be adding this code to the `plugin` section of your hyprland configuration file:  

```ini
# .config/hypr/hyprland.conf
plugin {
    hyprexpo {
        columns = 3
        gap_size = 5
        gap_size_outer = 0
        bg_col = rgb(111111)
        preview_mode = live # live or cached
        workspace_method = center current # [center/first] [workspace] e.g. first 1 or center m+1

        label_enable = false
        label_text_mode = id # token, index, or id
        selection_label_enable = false
        selection_label_token_map = a,s,d,f,g,q,w,e,r,t,z,x,c,v,b
        border_width = 0
        border_color_current = rgb(66ccff)
        border_color_hover = rgb(aabbcc)
        border_color_focus = rgb(ffcc66)
        window_icon_enable = false
        window_icon_position = bottom-right

        gesture_distance = 300 # how far is the "max" for the gesture
        cancel_key = escape # key that cancels the overview without selecting a workspace
    }
}
```

### Properties

| property | type | description | default |
| --- | --- | --- | --- |
columns | number | how many desktops are displayed on one line | `3`
gap_size | number | gap between desktops | `5`
gap_size_outer | number | gap around the outside of the grid | `0`
bg_col | color | color in gaps (between desktops) | `rgb(000000)`
workspace_method | [center/first] [workspace] | position of the desktops; comma-separated monitor-specific entries are supported, e.g. `DP-1 first 1, HDMI-A-1 center current, center current` | `center current`
preview_mode | string | preview backend: `live` re-renders workspaces every frame; `cached` uses framebuffer snapshots | `live`
skip_empty | boolean | whether the grid displays workspaces sequentially by id using selector "r" (`false`) or skips empty workspaces using selector "m" (`true`) | `false`
max_workspace | number | highest normal workspace to show when `skip_empty` is `false`; `0` disables the limit | `0`
show_workspace_numbers | boolean | legacy shortcut to show workspace ID labels | `false`
workspace_number_color | color | color of workspace number labels | `rgb(ffffff)`
gesture_distance | number | how far is the max for the gesture | `300`
cancel_key | string | key that cancels the overview without selecting a workspace; comma-separated keys are supported, and `none` disables it | `escape`
tile_rounding | number | corner radius for workspace tiles | `0`
tile_rounding_power | number | rounding curve exponent | `2.0`
tile_rounding_hover | number | hover tile radius; `-1` inherits `tile_rounding` | `-1`
tile_rounding_focus | number | keyboard-focused tile radius; `-1` inherits `tile_rounding` | `-1`
tile_rounding_current | number | current tile radius; `-1` inherits `tile_rounding` | `-1`
border_width | number | width of current, hover, and keyboard-focus highlight borders; `0` disables them | `0`
border_color_current | color | current workspace border color | `rgb(66ccff)`
border_color_hover | color | hovered workspace border color | `rgb(aabbcc)`
border_color_focus | color | keyboard-focused workspace border color | `rgb(ffcc66)`
window_icon_enable | boolean | overlay each previewed window's application icon | `false`
window_icon_position | string | icon corner within each previewed window: `top-left`, `top-right`, `bottom-left`, `bottom-right`, or `center` | `bottom-right`
window_icon_size | number | icon size | `32`
window_icon_offset_x | number | icon horizontal offset from the selected corner | `6`
window_icon_offset_y | number | icon vertical offset from the selected corner | `6`
window_icon_alpha | float | icon opacity from `0.0` to `1.0` | `1.0`
window_icon_bg_enable | boolean | draw a background behind window icons | `true`
window_icon_bg_color | color | window icon background color | `rgba(00000088)`
window_icon_bg_rounding | number | window icon background corner radius | `8`
window_icon_padding | number | window icon background padding | `4`
label_enable | boolean | show configurable workspace labels | `false`
label_text_mode | string | label content: `token`, `index`, or `id` | `token`
label_token_map | string | comma-separated token overrides for `label_text_mode = token`; empty entries hide that token | empty
label_position | string | `top-left`, `top-right`, `bottom-left`, `bottom-right`, or `center` | `top-left`
label_offset_x | number | label horizontal offset | `6`
label_offset_y | number | label vertical offset | `6`
selection_label_enable | boolean | show a separate selection-key label on each visible workspace | `false`
selection_label_token_map | string | comma-separated tokens used by `hyprexpo:kb_select` when selection labels are enabled; empty entries are unselectable | `a,s,d,f,g,q,w,e,r,t,z,x,c,v,b`
selection_label_position | string | `top-left`, `top-right`, `bottom-left`, `bottom-right`, or `center` | `top-right`
selection_label_offset_x | number | selection label horizontal offset | `6`
selection_label_offset_y | number | selection label vertical offset | `6`
selection_label_color | color | selection label color | `rgb(ffcc66)`
label_show | string | `always`, `hover`, `focus`, `hover+focus`, `current+focus`, or `never` | `always`
label_font_size | number | label font size | `16`
label_font_family | string | Pango font family | `Sans`
label_font_bold | boolean | draw bold label text | `true`
label_font_italic | boolean | draw italic label text | `false`
label_color_default | color | default label color | `rgb(ffffff)`
label_color_hover | color | hover label color | `rgb(eeeeee)`
label_color_focus | color | keyboard-focused label color | `rgb(ffcc66)`
label_color_current | color | current workspace label color | `rgb(66ccff)`
label_bg_enable | boolean | draw a background behind labels | `true`
label_bg_color | color | label background color | `rgba(00000088)`
label_bg_rounding | number | label background corner radius | `8`
label_padding | number | label background padding | `6`
label_pixel_snap | boolean | snap label boxes to integer pixels | `true`
keynav_wrap_h | boolean | keyboard navigation wraps horizontally | `true`
keynav_wrap_v | boolean | keyboard navigation wraps vertically | `true`
keynav_reading_order | boolean | left/right navigation scans row-major order | `false`
live_preview_follow_focus | boolean | switch the active workspace as overview focus moves | `false`

### Keywords

| name | description | arguments |
| -- | -- | -- | 
| hyprexpo-gesture | same as gesture, but for hyprexpo gestures. Supports: `expo`. | Same as gesture |

### Binding
```bash
# hyprland.conf
bind = MODIFIER, KEY, hyprexpo:expo, OPTION
```

Example:  
```bash
# This will toggle HyprExpo when SUPER+g is pressed
bind = SUPER, g, hyprexpo:expo, toggle
# This will switch to workspace 1, using the overview animation if it is open
bind = SUPER, 1, hyprexpo:expo, 1
```

When the overview is already open, pressing a raw number key from `1` through `9`
selects that workspace directly. Keypad numbers are supported as well.

Keyboard navigation dispatchers are also available:

| dispatcher | argument | description |
| --- | --- | --- |
`hyprexpo:kb_focus` | `left`, `right`, `up`, or `down` | move keyboard focus between visible tiles
`hyprexpo:kb_confirm` | none | select the keyboard-focused tile
`hyprexpo:kb_select` | token | select by visible token; uses `selection_label_token_map` when selection labels are enabled, otherwise `1-9`, `0`, then `a-z`
`hyprexpo:kb_selecti` | number | select by 1-based visible index

Lua config:
```lua
hl.config({
    plugin = {
        hyprexpo = {
            columns = 3,
            gap_size = 5,
            gap_size_outer = 0,
            bg_col = "rgb(111111)",
            workspace_method = "center current",
            skip_empty = false,
            max_workspace = 0,
            show_workspace_numbers = false,
            workspace_number_color = "rgb(ffffff)",
            window_icon_enable = false,
            window_icon_position = "bottom-right",
            window_icon_size = 32,
            label_enable = false,
            label_text_mode = "id",
            label_token_map = "",
            selection_label_enable = false,
            selection_label_token_map = "a,s,d,f,g,q,w,e,r,t,z,x,c,v,b",
            gesture_distance = 300,
            cancel_key = "escape",
        },
    },
})

hl.bind("SUPER + g", function()
    hl.plugin.hyprexpo.expo("toggle")
end)
```

Here are a list of options you can use:  
| option | description |
| --- | --- |
toggle | displays if hidden, hide if displayed
select | selects the hovered desktop
bring | brings a window from the hovered desktop to the current desktop
cancel | hides the overview without selecting a workspace
1-9 | switches to that workspace, using the overview animation if it is open
off | hides the overview
disable | same as `off`
on | displays the overview
enable | same as `on`
