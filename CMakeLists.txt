cmake_minimum_required(VERSION 3.27)

project(hyprexpo
    DESCRIPTION "hyprexpo plugin for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

set(SRC
    CachedPreview.cpp
    Config.cpp
    Dispatchers.cpp
    ExpoGesture.cpp
    GestureKeyword.cpp
    Hooks.cpp
    Internals.cpp
    Interaction.cpp
    LabelRenderer.cpp
    LivePreview.cpp
    Overview.cpp
    OverviewPassElement.cpp
    Plugin.cpp
    PreviewMode.cpp
    Render.cpp
    Swipe.cpp
    WorkspaceLayout.cpp
)

add_library(hyprexpo SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    hyprgraphics
    gio-unix-2.0
    libdrm
    libinput
    libudev
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)
target_link_libraries(hyprexpo PRIVATE rt PkgConfig::deps)

install(TARGETS hyprexpo)
