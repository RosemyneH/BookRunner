#include "context.h"

#include "candidates.h"
#include "files_search.h"
#include "input_xkb.h"
#include "render.h"

#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include "cursor-shape-v1-client-protocol.h"
#include "ext-background-effect-v1-client-protocol.h"

static int br_wheel_select_delta(const AppContext *ctx, int physical_step) {
  return ctx->config.invert_list_wheel ? -physical_step : physical_step;
}

static void br_buffer_release(void *data, struct wl_buffer *buffer) {
  AppContext *ctx = data;
  for (int i = 0; i < BR_FRAMEBUF_N; i++) {
    if (ctx->framebufs[i].wlbuf == buffer) {
      ctx->framebufs[i].busy = false;
      break;
    }
  }
  if (bookrunner_list_anim_pending(ctx)) {
    ctx->needs_draw = true;
  }
}

static const struct wl_buffer_listener br_buffer_listener = {
    .release = br_buffer_release,
};

static int br_create_shm(off_t size) {
  int fd = memfd_create("bookrunner-shm", MFD_CLOEXEC);
  if (fd < 0) {
    return -1;
  }
  if (ftruncate(fd, size) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static int64_t br_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void br_slot_destroy(ShmBuffer *s) {
  if (s->wlbuf) {
    wl_buffer_destroy(s->wlbuf);
    s->wlbuf = NULL;
  }
  if (s->data) {
    munmap(s->data, s->nbytes);
    s->data = NULL;
    s->nbytes = 0;
  }
  if (s->fd >= 0) {
    close(s->fd);
    s->fd = -1;
  }
  s->width = 0;
  s->height = 0;
  s->busy = false;
}

static void br_buffers_destroy_all(AppContext *ctx) {
  for (int i = 0; i < BR_FRAMEBUF_N; i++) {
    br_slot_destroy(&ctx->framebufs[i]);
  }
}

static bool br_slot_alloc(AppContext *ctx, ShmBuffer *s, int w, int h) {
  br_slot_destroy(s);
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
  if (stride <= 0) {
    return false;
  }
  size_t nbytes = (size_t)stride * (size_t)h;
  int fd = br_create_shm((off_t)nbytes);
  if (fd < 0) {
    return false;
  }
  void *data = mmap(NULL, nbytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close(fd);
    return false;
  }
  struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, (int32_t)nbytes);
  struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  wl_buffer_add_listener(buf, &br_buffer_listener, ctx);
  s->fd = fd;
  s->data = data;
  s->nbytes = nbytes;
  s->wlbuf = buf;
  s->width = w;
  s->height = h;
  s->busy = false;
  return true;
}

static ShmBuffer *br_framebuf_acquire(AppContext *ctx, int w, int h) {
  if (w <= 0 || h <= 0 || !ctx->shm) {
    return NULL;
  }
  for (int attempt = 0; attempt < 2; attempt++) {
    for (int i = 0; i < BR_FRAMEBUF_N; i++) {
      ShmBuffer *s = &ctx->framebufs[i];
      if (s->busy) {
        continue;
      }
      if (s->wlbuf && s->width == w && s->height == h) {
        return s;
      }
    }
    for (int i = 0; i < BR_FRAMEBUF_N; i++) {
      ShmBuffer *s = &ctx->framebufs[i];
      if (s->busy) {
        continue;
      }
      if (!br_slot_alloc(ctx, s, w, h)) {
        return NULL;
      }
      return s;
    }
    wl_display_dispatch_pending(ctx->display);
  }
  return NULL;
}

static void br_apply_default_pointer_cursor(AppContext *ctx, struct wl_pointer *wp, uint32_t serial) {
  if (ctx->cursor_shape_device) {
    wp_cursor_shape_device_v1_set_shape(ctx->cursor_shape_device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
    return;
  }
  if (!ctx->compositor || !ctx->shm) {
    return;
  }
  if (!ctx->cursor_surface) {
    ctx->cursor_surface = wl_compositor_create_surface(ctx->compositor);
    if (!ctx->cursor_surface) {
      return;
    }
  }
  if (!ctx->cursor_theme) {
    ctx->cursor_theme = wl_cursor_theme_load(NULL, 24, ctx->shm);
  }
  if (!ctx->cursor_theme) {
    return;
  }
  struct wl_cursor *cur = wl_cursor_theme_get_cursor(ctx->cursor_theme, "left_ptr");
  if (!cur || cur->image_count < 1) {
    cur = wl_cursor_theme_get_cursor(ctx->cursor_theme, "default");
  }
  if (!cur || cur->image_count < 1) {
    return;
  }
  struct wl_cursor_image *img = cur->images[0];
  struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
  if (!buf) {
    return;
  }
  wl_surface_attach(ctx->cursor_surface, buf, 0, 0);
  wl_surface_damage(ctx->cursor_surface, 0, 0, (int32_t)img->width, (int32_t)img->height);
  wl_surface_commit(ctx->cursor_surface);
  wl_pointer_set_cursor(wp, serial, ctx->cursor_surface, (int32_t)img->hotspot_x, (int32_t)img->hotspot_y);
}

static void br_bg_effect_attach(AppContext *ctx) {
  if (!ctx->bg_effect_blur_capable || !ctx->bg_effect_manager || !ctx->surface || ctx->bg_effect) {
    return;
  }
  ctx->bg_effect = ext_background_effect_manager_v1_get_background_effect(ctx->bg_effect_manager, ctx->surface);
  ctx->compositor_blur = true;
}

static void bg_effect_capabilities(void *data, struct ext_background_effect_manager_v1 *manager, uint32_t flags) {
  (void)manager;
  AppContext *ctx = data;
  ctx->bg_effect_blur_capable = (flags & EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR) != 0;
  if (!ctx->bg_effect_blur_capable) {
    ctx->compositor_blur = false;
  }
  br_bg_effect_attach(ctx);
}

static const struct ext_background_effect_manager_v1_listener bg_effect_manager_listener = {
    .capabilities = bg_effect_capabilities,
};

static void br_surface_apply_blur_region(AppContext *ctx) {
  if (!ctx->bg_effect || !ctx->compositor) {
    return;
  }
  int w = ctx->surf_width;
  int h = ctx->surf_height;
  if (w <= 0 || h <= 0) {
    return;
  }
  int rx, ry, rw, rh;
  bookrunner_input_region_extents(ctx, w, h, &rx, &ry, &rw, &rh);
  struct wl_region *reg = wl_compositor_create_region(ctx->compositor);
  if (!reg) {
    return;
  }
  wl_region_add(reg, rx, ry, rw, rh);
  ext_background_effect_surface_v1_set_blur_region(ctx->bg_effect, reg);
  wl_region_destroy(reg);
}

static void br_surface_apply_input_region(AppContext *ctx) {
  if (!ctx->surface || !ctx->compositor) {
    return;
  }
  int w = ctx->surf_width;
  int h = ctx->surf_height;
  if (w <= 0 || h <= 0) {
    return;
  }
  int rx, ry, rw, rh;
  bookrunner_input_region_extents(ctx, w, h, &rx, &ry, &rw, &rh);
  struct wl_region *reg = wl_compositor_create_region(ctx->compositor);
  if (!reg) {
    return;
  }
  wl_region_add(reg, rx, ry, rw, rh);
  wl_surface_set_input_region(ctx->surface, reg);
  wl_region_destroy(reg);
}

static void br_surface_sync_size(AppContext *ctx) {
  if (!ctx->layer_surface) {
    return;
  }
  int w = ctx->config.ui_width;
  int h = bookrunner_desired_height(ctx, w);
  if (w == ctx->surf_width && h == ctx->surf_height) {
    return;
  }
  ctx->list_scroll_init_done = false;
  zwlr_layer_surface_v1_set_size(ctx->layer_surface, (uint32_t)w, (uint32_t)h);
}

static void br_surface_paint(AppContext *ctx) {
  if (!ctx->surface || !ctx->shm) {
    return;
  }
  int w = ctx->surf_width;
  int h = ctx->surf_height;
  ShmBuffer *slot = br_framebuf_acquire(ctx, w, h);
  if (!slot || !slot->data) {
    wl_display_dispatch_pending(ctx->display);
    return;
  }
  int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
  cairo_surface_t *surf = cairo_image_surface_create_for_data(
      slot->data, CAIRO_FORMAT_ARGB32, w, h, stride);
  cairo_t *cr = cairo_create(surf);
  bookrunner_paint(ctx, cr, w, h);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  br_surface_apply_input_region(ctx);
  br_surface_apply_blur_region(ctx);
  wl_surface_attach(ctx->surface, slot->wlbuf, 0, 0);
  wl_surface_damage(ctx->surface, 0, 0, w, h);
  wl_surface_commit(ctx->surface);
  slot->busy = true;
  ctx->needs_draw = false;
  if (bookrunner_list_anim_pending(ctx)) {
    ctx->needs_draw = true;
  }
}

static void buf_del_last_char(char *buf) {
  if (!buf[0]) {
    return;
  }
  char *end = buf + strlen(buf);
  char *prev = g_utf8_find_prev_char(buf, end);
  *prev = '\0';
}

static void buf_append_cp(char *buf, size_t cap, uint32_t cp) {
  if (cp < 32 || cp == 127) {
    return;
  }
  char utfbuf[8];
  gint n = g_unichar_to_utf8((gunichar)cp, utfbuf);
  if (n <= 0) {
    return;
  }
  size_t cur = strlen(buf);
  if (cur + (size_t)n >= cap - 1) {
    return;
  }
  memcpy(buf + cur, utfbuf, (size_t)n);
  buf[cur + (size_t)n] = '\0';
}

static void query_del_char(AppContext *ctx) {
  buf_del_last_char(ctx->query);
}

static void input_backspace_step(AppContext *ctx) {
  if (br_ctx_bang_followup_active(ctx)) {
    if (!ctx->bang_follow_q[0]) {
      br_ctx_bang_followup_cancel(ctx);
      return;
    }
    buf_del_last_char(ctx->bang_follow_q);
  } else {
    if (!ctx->query[0]) {
      return;
    }
    query_del_char(ctx);
  }
  br_ctx_refilter(ctx);
  br_file_search_on_query_changed(ctx);
}

static void backspace_repeat_tick(AppContext *ctx) {
  if (!ctx->backspace_held) {
    return;
  }
  int64_t now = br_now_ms();
  if (now - ctx->backspace_press_ms < 300) {
    return;
  }
  if (now - ctx->backspace_last_repeat_ms < 40) {
    return;
  }
  ctx->backspace_last_repeat_ms = now;
  input_backspace_step(ctx);
}

static void query_append_cp(AppContext *ctx, uint32_t cp) {
  buf_append_cp(ctx->query, sizeof ctx->query, cp);
}

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
  (void)keyboard;
  AppContext *ctx = data;
  if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && fd >= 0 && size > 0) {
    input_xkb_set_keymap(&ctx->ixkb, fd, size);
  } else if (fd >= 0) {
    close(fd);
  }
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)surface;
  (void)keys;
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)surface;
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
  (void)keyboard;
  (void)serial;
  (void)time;
  AppContext *ctx = data;
  input_xkb_key(&ctx->ixkb, key, state);
  xkb_keysym_t sym = input_xkb_key_sym(&ctx->ixkb, key);
  if (sym == XKB_KEY_BackSpace) {
    if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
      ctx->backspace_held = false;
      return;
    }
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && !ctx->backspace_held) {
      ctx->backspace_held = true;
      ctx->backspace_press_ms = br_now_ms();
      ctx->backspace_last_repeat_ms = ctx->backspace_press_ms;
      input_backspace_step(ctx);
    }
    return;
  }
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
    return;
  }
  uint32_t utf = 0;
  if (ctx->ixkb.state) {
    utf = xkb_state_key_get_utf32(ctx->ixkb.state, key + 8);
  }
  if (sym == XKB_KEY_Escape) {
    ctx->done = true;
    ctx->exit_code = 1;
    return;
  }
  if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter || sym == XKB_KEY_grave || utf == '`') {
    br_ctx_submit(ctx);
    return;
  }
  if (sym == XKB_KEY_Up) {
    br_ctx_select_move(ctx, -1);
    return;
  }
  if (sym == XKB_KEY_Down) {
    br_ctx_select_move(ctx, 1);
    return;
  }
  if (sym == XKB_KEY_Insert) {
    if (br_ctx_ignore_selected_app(ctx)) {
      return;
    }
  }
  if (input_xkb_mod_ctrl(&ctx->ixkb) && sym == XKB_KEY_u) {
    if (br_ctx_bang_followup_active(ctx)) {
      ctx->bang_follow_q[0] = '\0';
    } else {
      ctx->query[0] = '\0';
    }
    br_ctx_refilter(ctx);
    br_file_search_on_query_changed(ctx);
    return;
  }
  if (!ctx->ixkb.state) {
    return;
  }
  if (utf != 0 && !input_xkb_mod_ctrl(&ctx->ixkb)) {
    if (br_ctx_bang_followup_active(ctx)) {
      buf_append_cp(ctx->bang_follow_q, sizeof ctx->bang_follow_q, utf);
    } else {
      query_append_cp(ctx, utf);
    }
    br_ctx_refilter(ctx);
    br_file_search_on_query_changed(ctx);
  }
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
  (void)data;
  (void)keyboard;
  (void)serial;
  (void)mods_depressed;
  (void)mods_latched;
  (void)mods_locked;
  (void)group;
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
  (void)data;
  (void)keyboard;
  (void)rate;
  (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
  AppContext *ctx = data;
  (void)surface;
  ctx->ptr_x = wl_fixed_to_double(sx);
  ctx->ptr_y = wl_fixed_to_double(sy);
  br_apply_default_pointer_cursor(ctx, wl_pointer, serial);
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {
  (void)data;
  (void)wl_pointer;
  (void)serial;
  (void)surface;
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
  (void)wl_pointer;
  (void)time;
  AppContext *ctx = data;
  ctx->ptr_x = wl_fixed_to_double(sx);
  ctx->ptr_y = wl_fixed_to_double(sy);
}

static uint32_t br_btn_prev_time;
static int br_btn_prev_row;
static bool br_btn_prev_valid;

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
  (void)wl_pointer;
  (void)serial;
  AppContext *ctx = data;
  if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
    int row = 0;
    if (bookrunner_pointer_pick_row(ctx, ctx->surf_width, ctx->surf_height, ctx->ptr_x, ctx->ptr_y, &row)) {
      uint32_t dt = time - br_btn_prev_time;
      if (br_btn_prev_valid && br_btn_prev_row == row && ctx->selected == row && dt < 450) {
        br_ctx_submit(ctx);
        br_btn_prev_valid = false;
        return;
      }
      ctx->selected = row;
      ctx->sel_pulse = 1.0;
      ctx->list_anim_last_ms = 0;
      ctx->list_scroll_interact_ms = 0;
      br_btn_prev_time = time;
      br_btn_prev_row = row;
      br_btn_prev_valid = true;
      ctx->needs_draw = true;
    } else {
      br_btn_prev_valid = false;
    }
  }
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
  (void)data;
  (void)wl_pointer;
  (void)time;
  (void)axis;
  (void)value;
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {
  (void)data;
  (void)wl_pointer;
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
  (void)data;
  (void)wl_pointer;
  (void)axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis) {
  (void)wl_pointer;
  (void)time;
  AppContext *ctx = data;
  if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
    ctx->list_scroll_interact_ms = 0;
    ctx->needs_draw = true;
  }
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t discrete) {
  (void)wl_pointer;
  AppContext *ctx = data;
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || discrete == 0) {
    return;
  }
  br_ctx_select_move(ctx, br_wheel_select_delta(ctx, discrete > 0 ? 1 : -1));
}

static void pointer_handle_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis, int32_t value120) {
  (void)wl_pointer;
  AppContext *ctx = data;
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL || value120 == 0) {
    return;
  }
  ctx->scroll_value120_accum += value120;
  while (ctx->scroll_value120_accum >= 120) {
    br_ctx_select_move(ctx, br_wheel_select_delta(ctx, 1));
    ctx->scroll_value120_accum -= 120;
  }
  while (ctx->scroll_value120_accum <= -120) {
    br_ctx_select_move(ctx, br_wheel_select_delta(ctx, -1));
    ctx->scroll_value120_accum += 120;
  }
}

static void pointer_handle_axis_relative_direction(void *data, struct wl_pointer *wl_pointer, uint32_t axis, uint32_t direction) {
  (void)data;
  (void)wl_pointer;
  (void)axis;
  (void)direction;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
    .axis_value120 = pointer_handle_axis_value120,
    .axis_relative_direction = pointer_handle_axis_relative_direction,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
  AppContext *ctx = data;
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ctx->pointer) {
    ctx->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(ctx->pointer, &pointer_listener, ctx);
    if (ctx->cursor_shape_manager) {
      ctx->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(ctx->cursor_shape_manager, ctx->pointer);
    }
  } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && ctx->pointer) {
    if (ctx->cursor_shape_device) {
      wp_cursor_shape_device_v1_destroy(ctx->cursor_shape_device);
      ctx->cursor_shape_device = NULL;
    }
    wl_pointer_destroy(ctx->pointer);
    ctx->pointer = NULL;
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->keyboard) {
    ctx->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
  } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && ctx->keyboard) {
    wl_keyboard_destroy(ctx->keyboard);
    ctx->keyboard = NULL;
  }
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
  (void)data;
  (void)seat;
  (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t width, uint32_t height) {
  AppContext *ctx = data;
  int w = width ? (int)width : ctx->config.ui_width;
  int h = height ? (int)height : ctx->config.ui_height;
  ctx->surf_width = w;
  ctx->surf_height = h;
  ctx->list_scroll_init_done = false;
  zwlr_layer_surface_v1_ack_configure(surface, serial);
  ctx->last_serial = serial;
  ctx->needs_draw = true;
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
  (void)surface;
  AppContext *ctx = data;
  ctx->done = true;
  ctx->exit_code = 0;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void registry_handle_global(void *data, struct wl_registry *reg, uint32_t name, const char *interface, uint32_t version) {
  AppContext *ctx = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    ctx->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    ctx->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    ctx->seat = wl_registry_bind(reg, name, &wl_seat_interface, 9);
    ctx->seat_name = name;
    wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
  } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
    uint32_t ver = version < 4 ? version : 4;
    ctx->layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, ver);
    ctx->layer_shell_name = name;
  } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
    uint32_t ver = version < 2 ? version : 2;
    ctx->cursor_shape_manager = wl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, ver);
    ctx->cursor_shape_mgr_name = name;
    if (ctx->pointer && !ctx->cursor_shape_device) {
      ctx->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(ctx->cursor_shape_manager, ctx->pointer);
    }
  } else if (strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
    uint32_t ver = version < 1 ? version : 1;
    ctx->bg_effect_manager = wl_registry_bind(reg, name, &ext_background_effect_manager_v1_interface, ver);
    ctx->bg_effect_mgr_name = name;
    ext_background_effect_manager_v1_add_listener(ctx->bg_effect_manager, &bg_effect_manager_listener, ctx);
  }
}

static void registry_handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
  AppContext *ctx = data;
  (void)reg;
  if (name == ctx->seat_name && ctx->seat) {
    if (ctx->pointer) {
      if (ctx->cursor_shape_device) {
        wp_cursor_shape_device_v1_destroy(ctx->cursor_shape_device);
        ctx->cursor_shape_device = NULL;
      }
      wl_pointer_destroy(ctx->pointer);
      ctx->pointer = NULL;
    }
    if (ctx->keyboard) {
      wl_keyboard_destroy(ctx->keyboard);
      ctx->keyboard = NULL;
    }
    wl_seat_destroy(ctx->seat);
    ctx->seat = NULL;
  }
  if (name == ctx->layer_shell_name && ctx->layer_shell) {
    zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    ctx->layer_shell = NULL;
  }
  if (name == ctx->cursor_shape_mgr_name && ctx->cursor_shape_manager) {
    if (ctx->cursor_shape_device) {
      wp_cursor_shape_device_v1_destroy(ctx->cursor_shape_device);
      ctx->cursor_shape_device = NULL;
    }
    wp_cursor_shape_manager_v1_destroy(ctx->cursor_shape_manager);
    ctx->cursor_shape_manager = NULL;
  }
  if (name == ctx->bg_effect_mgr_name && ctx->bg_effect_manager) {
    if (ctx->bg_effect) {
      ext_background_effect_surface_v1_destroy(ctx->bg_effect);
      ctx->bg_effect = NULL;
    }
    ext_background_effect_manager_v1_destroy(ctx->bg_effect_manager);
    ctx->bg_effect_manager = NULL;
    ctx->bg_effect_blur_capable = false;
    ctx->compositor_blur = false;
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void br_wayland_teardown(AppContext *ctx) {
  br_buffers_destroy_all(ctx);
  if (ctx->bg_effect) {
    ext_background_effect_surface_v1_destroy(ctx->bg_effect);
    ctx->bg_effect = NULL;
  }
  if (ctx->bg_effect_manager) {
    ext_background_effect_manager_v1_destroy(ctx->bg_effect_manager);
    ctx->bg_effect_manager = NULL;
  }
  ctx->compositor_blur = false;
  if (ctx->layer_surface) {
    zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    ctx->layer_surface = NULL;
  }
  if (ctx->surface) {
    wl_surface_destroy(ctx->surface);
    ctx->surface = NULL;
  }
  if (ctx->keyboard) {
    wl_keyboard_destroy(ctx->keyboard);
    ctx->keyboard = NULL;
  }
  if (ctx->cursor_shape_device) {
    wp_cursor_shape_device_v1_destroy(ctx->cursor_shape_device);
    ctx->cursor_shape_device = NULL;
  }
  if (ctx->pointer) {
    wl_pointer_destroy(ctx->pointer);
    ctx->pointer = NULL;
  }
  if (ctx->cursor_theme) {
    wl_cursor_theme_destroy(ctx->cursor_theme);
    ctx->cursor_theme = NULL;
  }
  if (ctx->cursor_surface) {
    wl_surface_destroy(ctx->cursor_surface);
    ctx->cursor_surface = NULL;
  }
  if (ctx->seat) {
    wl_seat_destroy(ctx->seat);
    ctx->seat = NULL;
  }
  if (ctx->cursor_shape_manager) {
    wp_cursor_shape_manager_v1_destroy(ctx->cursor_shape_manager);
    ctx->cursor_shape_manager = NULL;
  }
  if (ctx->layer_shell) {
    zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    ctx->layer_shell = NULL;
  }
  if (ctx->shm) {
    wl_shm_destroy(ctx->shm);
    ctx->shm = NULL;
  }
  if (ctx->compositor) {
    wl_compositor_destroy(ctx->compositor);
    ctx->compositor = NULL;
  }
  if (ctx->registry) {
    wl_registry_destroy(ctx->registry);
    ctx->registry = NULL;
  }
  if (ctx->display) {
    wl_display_disconnect(ctx->display);
    ctx->display = NULL;
  }
  input_xkb_fini(&ctx->ixkb);
}

static void br_instance_accept_toggle(AppContext *ctx) {
  if (ctx->instance_listen_fd < 0) {
    return;
  }
  for (;;) {
    int c = accept4(ctx->instance_listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (c < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      break;
    }
    char buf[32];
    (void)read(c, buf, sizeof buf);
    close(c);
    ctx->done = true;
    ctx->exit_code = 1;
  }
}

int bookrunner_wayland_run(AppContext *ctx) {
  for (int i = 0; i < BR_FRAMEBUF_N; i++) {
    ctx->framebufs[i].fd = -1;
    ctx->framebufs[i].wlbuf = NULL;
    ctx->framebufs[i].data = NULL;
    ctx->framebufs[i].nbytes = 0;
    ctx->framebufs[i].busy = false;
    ctx->framebufs[i].width = 0;
    ctx->framebufs[i].height = 0;
  }
  ctx->display = wl_display_connect(NULL);
  if (!ctx->display) {
    return 2;
  }
  input_xkb_init(&ctx->ixkb);
  ctx->registry = wl_display_get_registry(ctx->display);
  wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
  wl_display_roundtrip(ctx->display);
  if (!ctx->compositor || !ctx->shm || !ctx->layer_shell) {
    br_wayland_teardown(ctx);
    return 3;
  }
  ctx->surface = wl_compositor_create_surface(ctx->compositor);
  br_bg_effect_attach(ctx);
  ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
      ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "bookrunner");
  zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);
  zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, 0);
  zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, 0);
  zwlr_layer_surface_v1_set_margin(ctx->layer_surface, 0, 0, 0, 0);
  uint32_t ls_ver = wl_proxy_get_version((struct wl_proxy *)ctx->layer_surface);
  if (ls_ver >= ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND_SINCE_VERSION) {
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND);
  } else {
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
  }
  {
    int w = ctx->config.ui_width;
    int h = bookrunner_desired_height(ctx, w);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, (uint32_t)w, (uint32_t)h);
  }
  wl_surface_commit(ctx->surface);
  wl_display_roundtrip(ctx->display);
  if (ctx->surf_width <= 0) {
    ctx->surf_width = ctx->config.ui_width;
  }
  if (ctx->surf_height <= 0) {
    ctx->surf_height = bookrunner_desired_height(ctx, ctx->surf_width);
  }
  ctx->needs_draw = true;

  while (!ctx->done) {
    br_file_search_poll(ctx);
    if (ctx->needs_resize) {
      br_surface_sync_size(ctx);
      ctx->needs_resize = false;
    }
    if (ctx->needs_draw) {
      br_surface_paint(ctx);
    }
    while (wl_display_prepare_read(ctx->display) != 0) {
      wl_display_dispatch_pending(ctx->display);
    }
    if (ctx->needs_resize) {
      br_surface_sync_size(ctx);
      ctx->needs_resize = false;
    }
    if (ctx->needs_draw) {
      br_surface_paint(ctx);
    }
    wl_display_flush(ctx->display);
    struct pollfd pfds[4];
    int n = 0;
    const int wl_i = 0;
    pfds[n].fd = wl_display_get_fd(ctx->display);
    pfds[n].events = POLLIN;
    n++;
    int inst_i = -1;
    if (ctx->instance_listen_fd >= 0) {
      inst_i = n;
      pfds[n].fd = ctx->instance_listen_fd;
      pfds[n].events = POLLIN;
      n++;
    }
    int file_i = -1;
    int nf = ctx->file_search.notify_pipe[0];
    if (nf >= 0) {
      file_i = n;
      pfds[n].fd = nf;
      pfds[n].events = POLLIN;
      n++;
    }
    int timeout_ms = -1;
    pthread_mutex_lock(&ctx->file_search.mx);
    if (ctx->file_search.want_search && !ctx->file_search.worker_live) {
      int64_t left = ctx->file_search.deadline_ms - br_now_ms();
      if (left < 0) {
        left = 0;
      }
      if (left > 2000) {
        left = 2000;
      }
      timeout_ms = (int)left;
    }
    pthread_mutex_unlock(&ctx->file_search.mx);
    if (bookrunner_list_anim_pending(ctx)) {
      if (timeout_ms < 0 || timeout_ms > 16) {
        timeout_ms = 16;
      }
    }
    if (ctx->backspace_held) {
      int64_t now = br_now_ms();
      int until = 16;
      int64_t held = now - ctx->backspace_press_ms;
      if (held < 300) {
        until = (int)(300 - held);
      } else {
        until = (int)(40 - (now - ctx->backspace_last_repeat_ms));
      }
      if (until < 1) {
        until = 1;
      }
      if (timeout_ms < 0 || timeout_ms > until) {
        timeout_ms = until;
      }
    }
    int pr = poll(pfds, (nfds_t)n, timeout_ms);
    if (pr < 0 && errno != EINTR) {
      break;
    }
    if (inst_i >= 0 && (pfds[inst_i].revents & POLLIN)) {
      br_instance_accept_toggle(ctx);
    }
    if (file_i >= 0 && (pfds[file_i].revents & POLLIN)) {
      br_file_search_drain_notify(ctx);
      br_ctx_refilter(ctx);
    }
    if (pfds[wl_i].revents & (POLLERR | POLLHUP)) {
      break;
    }
    if (pfds[wl_i].revents & POLLIN) {
      if (wl_display_read_events(ctx->display) != 0) {
        break;
      }
    } else {
      wl_display_cancel_read(ctx->display);
    }
    wl_display_dispatch_pending(ctx->display);
    backspace_repeat_tick(ctx);
    br_file_search_poll(ctx);
    if (ctx->needs_resize) {
      br_surface_sync_size(ctx);
      ctx->needs_resize = false;
    }
    if (ctx->needs_draw) {
      br_surface_paint(ctx);
    }
  }
  int code = ctx->exit_code;
  br_wayland_teardown(ctx);
  return code;
}
