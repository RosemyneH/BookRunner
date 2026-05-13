#pragma once

#include <cairo.h>
#include <stdbool.h>

typedef struct AppContext AppContext;

void bookrunner_paint(AppContext *ctx, cairo_t *cr, int width, int height);
bool bookrunner_pointer_pick_row(AppContext *ctx, int width, int height, double px, double py, int *out_row);
void bookrunner_input_region_extents(const AppContext *ctx, int width, int height, int *out_x, int *out_y, int *out_w, int *out_h);
void bookrunner_list_ensure_scroll(AppContext *ctx, int width, int height);
void bookrunner_list_anim_step(AppContext *ctx, int width, int height);
bool bookrunner_list_anim_pending(const AppContext *ctx);
