project(
  'bookrunner',
  'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2', 'werror=false'],
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

wayland_client = dependency('wayland-client')
wayland_cursor_dep = dependency('wayland-cursor')
cairo_dep = dependency('cairo')
pango_dep = dependency('pango')
pangocairo_dep = dependency('pangocairo')
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
xkbcommon_dep = dependency('xkbcommon')

wayland_scanner = find_program('wayland-scanner', native: true)
wl_proto = files('protocols/wlr-layer-shell-unstable-v1.xml')
cursor_shape_proto = files('protocols/cursor-shape-v1.xml')

wl_proto_h = custom_target(
  'wlr-layer-shell-client-protocol.h',
  input: wl_proto,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

wl_proto_c = custom_target(
  'wlr-layer-shell-client-protocol.c',
  input: wl_proto,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

cursor_shape_proto_h = custom_target(
  'cursor-shape-v1-client-protocol.h',
  input: cursor_shape_proto,
  output: 'cursor-shape-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

cursor_shape_proto_c = custom_target(
  'cursor-shape-v1-client-protocol.c',
  input: cursor_shape_proto,
  output: 'cursor-shape-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

bookrunner_inc = include_directories('src', '.')

thread_dep = dependency('threads')

executable(
  'bookrunner',
  [
    files(
      'src/main.c',
      'src/arena.c',
      'src/apps.c',
      'src/render.c',
      'src/wayland.c',
      'src/input_xkb.c',
      'src/config.c',
      'src/candidates.c',
      'src/files_search.c',
      'src/bangs.c',
      'src/xdg_popup_stub.c',
      'src/tablet_tool_v2_stub.c',
    ),
    wl_proto_c,
    wl_proto_h,
    cursor_shape_proto_c,
    cursor_shape_proto_h,
  ],
  include_directories: bookrunner_inc,
  dependencies: [
    wayland_client,
    wayland_cursor_dep,
    cairo_dep,
    pango_dep,
    pangocairo_dep,
    gdk_pixbuf_dep,
    gio_dep,
    gio_unix_dep,
    xkbcommon_dep,
    m_dep,
    thread_dep,
  ],
  c_args: ['-D_GNU_SOURCE'],
  install: true,
)
