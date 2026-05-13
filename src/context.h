#pragma once

#include "arena.h"
#include "apps.h"
#include "candidates.h"
#include "config.h"
#include "files_search.h"
#include "input_xkb.h"
#include "usage_db.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct wp_cursor_shape_manager_v1;
struct wp_cursor_shape_device_v1;
struct wl_cursor_theme;

#define BR_MAX_QUERY 512

typedef struct ShmBuffer ShmBuffer;
struct ShmBuffer {
  int fd;
  void *data;
  size_t nbytes;
  struct wl_buffer *wlbuf;
  int width;
  int height;
  bool busy;
};

typedef struct AppContext AppContext;
struct AppContext {
  GPtrArray *apps;
  BrUsageDb *usage_db;
  BrArena candidate_arena;
  GPtrArray *candidates;
  char query[BR_MAX_QUERY];
  int selected;
  double list_scroll_px;
  double list_scroll_goal_cached;
  double list_scroll_anim_px;
  bool list_scroll_init_done;
  double sel_pulse;
  int64_t list_anim_last_ms;
  int64_t list_scroll_interact_ms;
  bool done;
  int exit_code;
  int instance_listen_fd;

  GDesktopAppInfo *launch_app;
  char *launch_uri;
  char *launch_file;

  BrConfig config;
  BrFileSearch file_search;

  GdkPixbuf *icon_file;
  GdkPixbuf *icon_bang;

  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_shm *shm;
  struct wl_seat *seat;
  struct zwlr_layer_shell_v1 *layer_shell;
  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct wl_keyboard *keyboard;
  struct wl_pointer *pointer;
  double ptr_x;
  double ptr_y;
  int scroll_value120_accum;
  struct wl_registry *registry;
  uint32_t layer_shell_name;
  uint32_t seat_name;
  uint32_t cursor_shape_mgr_name;

  struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
  struct wp_cursor_shape_device_v1 *cursor_shape_device;
  struct wl_cursor_theme *cursor_theme;
  struct wl_surface *cursor_surface;

  ShmBuffer framebufs[3];
  int surf_width;
  int surf_height;
  bool needs_draw;
  uint32_t last_serial;

  InputXkb ixkb;
};

int bookrunner_wayland_run(AppContext *ctx);
