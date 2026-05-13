#pragma once

#include "config.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <stdbool.h>

typedef struct AppContext AppContext;

typedef enum {
  BR_CAND_APP = 1,
  BR_CAND_BANG,
  BR_CAND_FILE,
} BrCandKind;

typedef struct BrCandidate BrCandidate;
struct BrCandidate {
  BrCandKind kind;
  char *title;
  char *subtitle;
  GdkPixbuf *icon;
  guint app_index;
  char *open_uri;
  char *file_path;
};

void br_candidate_free(BrCandidate *c);

void br_ctx_refilter(AppContext *ctx);
void br_ctx_select_move(AppContext *ctx, int delta);
void br_ctx_activate(AppContext *ctx);
void br_ctx_free_launch_fields(AppContext *ctx);
BrCandidate *br_ctx_selected(const AppContext *ctx);
void br_ctx_candidates_clear(AppContext *ctx);
