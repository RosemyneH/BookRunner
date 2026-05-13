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
  BR_CAND_ACTION,
} BrCandKind;

typedef enum {
  BR_ACT_NONE = 0,
  BR_ACT_TOGGLE_BANG_F,
  BR_ACT_UNIGNORE,
} BrCandAct;

typedef struct BrCandidate BrCandidate;
struct BrCandidate {
  BrCandKind kind;
  BrCandAct act;
  char *title;
  char *subtitle;
  GdkPixbuf *icon;
  guint app_index;
  char *open_uri;
  char *bang_kw;
  char *file_path;
  char *action_id;
};

void br_ctx_refilter(AppContext *ctx);
void br_ctx_select_move(AppContext *ctx, int delta);
void br_ctx_submit(AppContext *ctx);
void br_ctx_activate(AppContext *ctx);
bool br_ctx_bang_followup_active(const AppContext *ctx);
void br_ctx_bang_followup_cancel(AppContext *ctx);
void br_ctx_bang_followup_launch(AppContext *ctx);
void br_ctx_free_launch_fields(AppContext *ctx);
BrCandidate *br_ctx_selected(const AppContext *ctx);
void br_ctx_candidates_clear(AppContext *ctx);
bool br_ctx_ignore_selected_app(AppContext *ctx);
