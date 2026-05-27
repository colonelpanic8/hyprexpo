project('hyprexpo', 'cpp',
  version: '0.1',
  default_options: ['buildtype=release'],
)

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++2b')
  add_global_arguments('-std=c++2b', language: 'cpp')
else
  error('Could not configure current C++ compiler (' + cpp_compiler.get_id() + ' ' + cpp_compiler.version() + ') with required C++ standard (C++23)')
endif

add_project_arguments(
  [
    '-Wno-narrowing',
  ],
  language: 'cpp')

src = [
  'CachedPreview.cpp',
  'Config.cpp',
  'Dispatchers.cpp',
  'ExpoGesture.cpp',
  'GestureKeyword.cpp',
  'Hooks.cpp',
  'Internals.cpp',
  'Interaction.cpp',
  'LabelRenderer.cpp',
  'LivePreview.cpp',
  'Overview.cpp',
  'OverviewPassElement.cpp',
  'Plugin.cpp',
  'PreviewMode.cpp',
  'Render.cpp',
  'Swipe.cpp',
  'WorkspaceLayout.cpp',
]

shared_module(meson.project_name(), src,
  dependencies: [
    dependency('hyprland'),
    dependency('hyprgraphics'),
    dependency('gio-unix-2.0'),
    dependency('pixman-1'),
    dependency('libdrm'),
    dependency('pangocairo'),
    dependency('libinput'),
    dependency('libudev'),
    dependency('wayland-server'),
    dependency('xkbcommon'),
  ],
  install: true,
)
