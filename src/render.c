#include "render.h"

#include "candidates.h"
#include "context.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void br_cairo_set_source_pixbuf(cairo_t *cr, GdkPixbuf *pb, double x, double y) {
  int w = gdk_pixbuf_get_width(pb);
  int h = gdk_pixbuf_get_height(pb);
  if (w <= 0 || h <= 0) {
    return;
  }
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  int ds = cairo_image_surface_get_stride(surf);
  guchar *dd = cairo_image_surface_get_data(surf);
  int ss = gdk_pixbuf_get_rowstride(pb);
  const guchar *sp = gdk_pixbuf_get_pixels(pb);
  int nc = gdk_pixbuf_get_n_channels(pb);
  gboolean ha = gdk_pixbuf_get_has_alpha(pb);
  for (int row = 0; row < h; row++) {
    const guchar *sr = sp + row * ss;
    guchar *dr = dd + row * ds;
    for (int col = 0; col < w; col++) {
      const guchar *s = sr + col * nc;
      guchar r = s[0];
      guchar g = s[1];
      guchar b = s[2];
      guchar a = ha ? s[3] : 255;
      dr[col * 4 + 0] = (guchar)((guint)b * a / 255);
      dr[col * 4 + 1] = (guchar)((guint)g * a / 255);
      dr[col * 4 + 2] = (guchar)((guint)r * a / 255);
      dr[col * 4 + 3] = a;
    }
  }
  cairo_surface_mark_dirty(surf);
  cairo_save(cr);
  cairo_rectangle(cr, x, y, w, h);
  cairo_clip(cr);
  cairo_set_source_surface(cr, surf, x, y);
  cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
  cairo_paint(cr);
  cairo_restore(cr);
  cairo_surface_destroy(surf);
}

static void cr_u32(cairo_t *cr, uint32_t c) {
  double a = ((c >> 24) & 0xff) / 255.0;
  double rr = ((c >> 16) & 0xff) / 255.0;
  double g = ((c >> 8) & 0xff) / 255.0;
  double b = (c & 0xff) / 255.0;
  cairo_set_source_rgba(cr, rr, g, b, a);
}

static void draw_round_rect(cairo_t *cr, double x, double y, double w, double h, double rad) {
  double r = fmin(rad, fmin(w, h) / 2.0);
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0);
  cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2.0);
  cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
  cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
  cairo_close_path(cr);
}

static void draw_text_shadowed(cairo_t *cr, PangoLayout *layout, double x, double y, uint32_t col) {
  cairo_save(cr);
  cairo_translate(cr, x, y);
  cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
  cairo_move_to(cr, 1, 1);
  pango_cairo_show_layout(cr, layout);
  cairo_move_to(cr, 0, 0);
  cr_u32(cr, col);
  pango_cairo_show_layout(cr, layout);
  cairo_restore(cr);
}

static void draw_icon_fit(cairo_t *cr, GdkPixbuf *pb, double x, double y, double size) {
  if (!pb) {
    return;
  }
  int iw = gdk_pixbuf_get_width(pb);
  int ih = gdk_pixbuf_get_height(pb);
  if (iw <= 0 || ih <= 0) {
    return;
  }
  double sx = size / (double)iw;
  double sy = size / (double)ih;
  double sc = fmin(sx, sy);
  double dw = iw * sc;
  double dh = ih * sc;
  double ox = x + (size - dw) * 0.5;
  double oy = y + (size - dh) * 0.5;
  cairo_save(cr);
  cairo_translate(cr, ox, oy);
  cairo_scale(cr, sc, sc);
  br_cairo_set_source_pixbuf(cr, pb, 0, 0);
  cairo_restore(cr);
}

static void draw_icon_circle(cairo_t *cr, GdkPixbuf *pb, double cx, double cy, double radius) {
  cairo_save(cr);
  cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
  cairo_clip(cr);
  draw_icon_fit(cr, pb, cx - radius, cy - radius, radius * 2);
  cairo_restore(cr);
  cairo_new_sub_path(cr);
  cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
  cr_u32(cr, 0x88000000u);
  cairo_set_line_width(cr, 2);
  cairo_stroke(cr);
}

typedef struct {
  double pad;
  double x0;
  double inner_w;
  double hero_r;
  double row_h;
  double input_h;
  double corner;
  double y0;
  double cy;
  double y_title;
  double y_input;
  double y_list;
} BrUILayout;

int64_t bookrunner_mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void br_ui_layout(const AppContext *ctx, int width, int height, BrUILayout *u) {
  const BrConfig *cfg = &ctx->config;
  (void)height;
  u->pad = 10;
  u->corner = 10;
  u->hero_r = 24;
  u->row_h = 28;
  u->input_h = 28;
  u->x0 = u->pad + 8;
  u->inner_w = width - 2 * u->pad - 16;
  u->y0 = u->pad + 6;
  u->cy = u->y0 + u->hero_r;
  u->y_title = u->y0 + u->hero_r * 2 + 7;
  u->y_input = u->y_title + cfg->font_size * 1.4 + 5;
  u->y_list = u->y_input + u->input_h + 6;
}

static void br_list_metrics(const AppContext *ctx, int width, int height, BrUILayout *u, double *y_list, double *list_h, double *row_h, int *visible_slots) {
  br_ui_layout(ctx, width, height, u);
  *y_list = u->y_list;
  *row_h = u->row_h;
  double bottom = (double)height - u->pad - 6.0;
  *list_h = bottom - *y_list;
  if (*list_h < *row_h) {
    *list_h = *row_h;
  }
  *visible_slots = (int)(*list_h / *row_h);
  if (*visible_slots < 1) {
    *visible_slots = 1;
  }
}

void bookrunner_list_ensure_scroll(AppContext *ctx, int width, int height) {
  if (!ctx->candidates || ctx->candidates->len == 0) {
    ctx->list_first_visible = 0;
    return;
  }
  BrUILayout u;
  double y_list, list_h, row_h;
  int visible;
  br_list_metrics(ctx, width, height, &u, &y_list, &list_h, &row_h, &visible);
  int n = (int)ctx->candidates->len;
  if (visible >= n) {
    ctx->list_first_visible = 0;
    return;
  }
  int max_first = n - visible;
  int64_t now = bookrunner_mono_ms();
  int fast = ctx->list_scroll_interact_ms != 0 && (now - ctx->list_scroll_interact_ms) < 100;
  if (fast) {
    if (ctx->selected < ctx->list_first_visible) {
      ctx->list_first_visible = ctx->selected;
    }
    if (ctx->selected >= ctx->list_first_visible + visible) {
      ctx->list_first_visible = ctx->selected - visible + 1;
    }
  } else {
    int goal = ctx->selected - visible / 2;
    if (goal < 0) {
      goal = 0;
    }
    if (goal > max_first) {
      goal = max_first;
    }
    if (goal != ctx->list_first_visible) {
      ctx->list_recenter_px += (double)(ctx->list_first_visible - goal) * row_h;
      ctx->list_first_visible = goal;
    }
  }
  if (ctx->list_first_visible < 0) {
    ctx->list_first_visible = 0;
  }
  if (ctx->list_first_visible > max_first) {
    ctx->list_first_visible = max_first;
  }
}

bool bookrunner_list_anim_pending(const AppContext *ctx) {
  return fabs(ctx->list_scroll_anim_px) > 0.02 || ctx->sel_pulse > 0.02 || fabs(ctx->list_recenter_px) > 0.08;
}

void bookrunner_list_anim_step(AppContext *ctx, int width, int height) {
  (void)width;
  (void)height;
  if (ctx->list_anim_last_ms == 0) {
    ctx->list_anim_last_ms = bookrunner_mono_ms();
  } else {
    int64_t now = bookrunner_mono_ms();
    double dt = ((double)(now - ctx->list_anim_last_ms)) / 1000.0;
    if (dt > 0) {
      if (dt > 1.0 / 60.0) {
        dt = 1.0 / 60.0;
      }
      ctx->list_anim_last_ms = now;
      if (fabs(ctx->list_scroll_anim_px) > 0.25) {
        ctx->list_scroll_anim_px *= exp(-dt * 18.0);
      } else {
        ctx->list_scroll_anim_px = 0;
      }
      if (ctx->sel_pulse > 0.03) {
        ctx->sel_pulse *= exp(-dt * 11.0);
      } else {
        ctx->sel_pulse = 0;
      }
      if (fabs(ctx->list_recenter_px) > 0.35) {
        ctx->list_recenter_px *= exp(-dt * 24.0);
      } else {
        ctx->list_recenter_px = 0;
      }
    }
  }
  if (!bookrunner_list_anim_pending(ctx)) {
    ctx->list_anim_last_ms = 0;
    ctx->list_scroll_anim_px = 0;
    ctx->sel_pulse = 0;
    ctx->list_recenter_px = 0;
  }
}

static uint32_t br_blend_u32(uint32_t base, uint32_t over, double t) {
  t = fmin(1.0, fmax(0.0, t));
  uint32_t ba = (base >> 24) & 0xff, br = (base >> 16) & 0xff, bg = (base >> 8) & 0xff, bb = base & 0xff;
  uint32_t oa = (over >> 24) & 0xff, ovrr = (over >> 16) & 0xff, ovg = (over >> 8) & 0xff, ovb = over & 0xff;
  uint32_t na = (uint32_t)(ba * (1 - t) + oa * t);
  uint32_t nr = (uint32_t)(br * (1 - t) + ovrr * t);
  uint32_t ng = (uint32_t)(bg * (1 - t) + ovg * t);
  uint32_t nb = (uint32_t)(bb * (1 - t) + ovb * t);
  return (na << 24) | (nr << 16) | (ng << 8) | nb;
}

void bookrunner_input_region_extents(const AppContext *ctx, int width, int height, int *out_x, int *out_y, int *out_w, int *out_h) {
  BrUILayout u;
  br_ui_layout(ctx, width, height, &u);
  int pad = (int)u.pad;
  int iw = width - 2 * pad;
  int ih = height - 2 * pad;
  if (iw < 1) {
    iw = 1;
  }
  if (ih < 1) {
    ih = 1;
  }
  *out_x = pad;
  *out_y = pad;
  *out_w = iw;
  *out_h = ih;
}

bool bookrunner_pointer_pick_row(AppContext *ctx, int width, int height, double px, double py, int *out_row) {
  if (!ctx->candidates || !out_row) {
    return false;
  }
  BrUILayout u;
  double y_list, list_h, row_h;
  int visible;
  br_list_metrics(ctx, width, height, &u, &y_list, &list_h, &row_h, &visible);
  if (px < u.x0 - 4 || px > u.x0 + u.inner_w + 4) {
    return false;
  }
  if (py < y_list || py > y_list + list_h) {
    return false;
  }
  const double list_y_off = ctx->list_scroll_anim_px + ctx->list_recenter_px;
  int local = (int)floor((py - y_list - list_y_off) / row_h);
  if (local < 0) {
    return false;
  }
  int g = ctx->list_first_visible + local;
  if (g < 0 || g >= (int)ctx->candidates->len) {
    return false;
  }
  *out_row = g;
  return true;
}

void bookrunner_paint(AppContext *ctx, cairo_t *cr, int width, int height) {
  const BrConfig *cfg = &ctx->config;
  bookrunner_list_anim_step(ctx, width, height);
  if (width > 0 && height > 0) {
    bookrunner_list_ensure_scroll(ctx, width, height);
  }
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  BrUILayout u;
  br_ui_layout(ctx, width, height, &u);

  cairo_save(cr);
  draw_round_rect(cr, u.pad, u.pad, width - 2 * u.pad, height - 2 * u.pad, u.corner);
  cr_u32(cr, cfg->col_panel);
  cairo_fill(cr);
  draw_round_rect(cr, u.pad, u.pad, width - 2 * u.pad, height - 2 * u.pad, u.corner);
  cr_u32(cr, cfg->col_border);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
  cairo_restore(cr);

  BrCandidate *sel = br_ctx_selected(ctx);
  GdkPixbuf *hero_pb = sel ? sel->icon : NULL;
  if (sel && sel->kind == BR_CAND_APP && hero_pb == NULL) {
    AppEntry *e = g_ptr_array_index(ctx->apps, sel->app_index);
    hero_pb = e->icon;
  }

  double cx = width * 0.5;
  draw_icon_circle(cr, hero_pb, cx, u.cy, u.hero_r);

  PangoLayout *title_lo = pango_cairo_create_layout(cr);
  PangoFontDescription *fd = pango_font_description_from_string(cfg->font);
  pango_font_description_set_absolute_size(fd, (int)(cfg->font_size * PANGO_SCALE * 1.28));
  pango_layout_set_font_description(title_lo, fd);
  const char *title = sel && sel->title ? sel->title : "BookRunner";
  pango_layout_set_width(title_lo, (int)(u.inner_w * PANGO_SCALE));
  pango_layout_set_alignment(title_lo, PANGO_ALIGN_CENTER);
  pango_layout_set_text(title_lo, title, -1);
  draw_text_shadowed(cr, title_lo, u.x0, u.y_title, cfg->col_text);
  g_object_unref(title_lo);
  pango_font_description_free(fd);

  cairo_save(cr);
  draw_round_rect(cr, u.x0, u.y_input, u.inner_w, u.input_h, 8);
  cr_u32(cr, cfg->col_input_bg);
  cairo_fill(cr);
  draw_round_rect(cr, u.x0, u.y_input, u.inner_w, u.input_h, 8);
  cr_u32(cr, cfg->col_border);
  cairo_set_line_width(cr, 1);
  cairo_stroke(cr);
  cairo_restore(cr);

  PangoLayout *qlo = pango_cairo_create_layout(cr);
  PangoFontDescription *qfd = pango_font_description_from_string(cfg->font);
  pango_font_description_set_absolute_size(qfd, (int)(cfg->font_size * PANGO_SCALE));
  pango_layout_set_font_description(qlo, qfd);
  pango_layout_set_width(qlo, (int)((u.inner_w - 16) * PANGO_SCALE));
  pango_layout_set_text(qlo, ctx->query, -1);
  draw_text_shadowed(cr, qlo, u.x0 + 8, u.y_input + 8, cfg->col_text);
  g_object_unref(qlo);
  pango_font_description_free(qfd);

  PangoLayout *rlo = pango_cairo_create_layout(cr);
  PangoFontDescription *rfd = pango_font_description_from_string(cfg->font);
  pango_font_description_set_absolute_size(rfd, (int)(cfg->font_size * PANGO_SCALE * 0.95));
  pango_layout_set_font_description(rlo, rfd);

  const double list_icon = 22;
  const double text_x = u.x0 + 6 + list_icon + 6;
  double y_list, list_h, row_h;
  int visible_slots;
  br_list_metrics(ctx, width, height, &u, &y_list, &list_h, &row_h, &visible_slots);
  int n = (int)ctx->candidates->len;
  const double scroll_track_x = u.x0 + u.inner_w - 3;
  const double scroll_track_w = 3.0;

  cairo_save(cr);
  cairo_rectangle(cr, u.x0 - 4, y_list, u.inner_w - 2, list_h);
  cairo_clip(cr);
  const double list_y_off = ctx->list_scroll_anim_px + ctx->list_recenter_px;
  for (int i = ctx->list_first_visible; i < n; i++) {
    double ry = y_list + (double)(i - ctx->list_first_visible) * row_h + list_y_off;
    if (ry > y_list + list_h) {
      break;
    }
    if (ry + row_h < y_list) {
      continue;
    }
    BrCandidate *c = g_ptr_array_index(ctx->candidates, (guint)i);
    if (i == ctx->selected) {
      uint32_t sel_col = br_blend_u32(cfg->col_row_sel, 0xffffffffu, ctx->sel_pulse * 0.22);
      cairo_save(cr);
      draw_round_rect(cr, u.x0 - 4, ry - 2, u.inner_w - 2, row_h - 2, 8);
      cr_u32(cr, sel_col);
      cairo_fill(cr);
      cairo_restore(cr);
    }
    GdkPixbuf *ic = c->icon;
    if (c->kind == BR_CAND_APP && !ic) {
      AppEntry *e = g_ptr_array_index(ctx->apps, c->app_index);
      ic = e->icon;
    }
    draw_icon_fit(cr, ic, u.x0 + 6, ry + 3, list_icon);
    pango_layout_set_text(rlo, c->title ? c->title : "", -1);
    draw_text_shadowed(cr, rlo, text_x, ry + 5, cfg->col_text);
  }
  cairo_restore(cr);

  if (n > visible_slots) {
    double track_top = y_list + 2;
    double track_h = list_h - 4;
    double thumb_h = fmax(row_h * 0.45, track_h * (double)visible_slots / (double)n);
    int max_first = n - visible_slots;
    double t = max_first > 0 ? (double)ctx->list_first_visible / (double)max_first : 0;
    t = fmin(1.0, fmax(0.0, t));
    double thumb_y = track_top + t * (track_h - thumb_h);
    cairo_save(cr);
    cr_u32(cr, cfg->col_border);
    cairo_rectangle(cr, scroll_track_x, track_top, scroll_track_w, track_h);
    cairo_fill(cr);
    cr_u32(cr, br_blend_u32(cfg->col_text, cfg->col_panel, 0.55));
    draw_round_rect(cr, scroll_track_x, thumb_y, scroll_track_w, thumb_h, 1.5);
    cairo_fill(cr);
    cairo_restore(cr);
  }

  pango_font_description_free(rfd);
  g_object_unref(rlo);
}
