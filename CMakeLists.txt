cmake_minimum_required(VERSION 3.27)

project(hyprexpo
    DESCRIPTION "hyprexpo plugin for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

set(SRC
    Config.cpp
    Dispatchers.cpp
    ExpoGesture.cpp
    GestureKeyword.cpp
    Hooks.cpp
    Internals.cpp
    Interaction.cpp
    LabelRenderer.cpp
    Overview.cpp
    OverviewPassElement.cpp
    Plugin.cpp
    Render.cpp
    Swipe.cpp
    WorkspaceLayout.cpp
)

add_library(hyprexpo SHARED ${SRC})

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
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
