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
        bg_col = rgb(111111)
        workspace_method = center current # [center/first] [workspace] e.g. first 1 or center m+1

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
bg_col | color | color in gaps (between desktops) | `rgb(000000)`
workspace_method | [center/first] [workspace] | position of the desktops | `center current`
skip_empty | boolean | whether the grid displays workspaces sequentially by id using selector "r" (`false`) or skips empty workspaces using selector "m" (`true`) | `false`
max_workspace | number | highest normal workspace to show when `skip_empty` is `false`; `0` disables the limit | `0`
show_workspace_numbers | boolean | show numeric labels for workspaces | `false`
workspace_number_color | color | color of workspace number labels | `rgb(ffffff)`
gesture_distance | number | how far is the max for the gesture | `300`
cancel_key | string | key that cancels the overview without selecting a workspace; comma-separated keys are supported, and `none` disables it | `escape`

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

Lua config:
```lua
hl.config({
    plugin = {
        hyprexpo = {
            columns = 3,
            gap_size = 5,
            bg_col = "rgb(111111)",
            workspace_method = "center current",
            skip_empty = false,
            max_workspace = 0,
            show_workspace_numbers = false,
            workspace_number_color = "rgb(ffffff)",
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
