#include "candidates.h"

#include "apps.h"
#include "bangs.h"
#include "context.h"
#include "render.h"

#include <stdlib.h>
#include <string.h>

static BrCandidate *alloc_candidate(AppContext *ctx) {
  BrCandidate *c =
      br_arena_alloc(&ctx->candidate_arena, sizeof(BrCandidate), _Alignof(BrCandidate));
  if (!c) {
    return NULL;
  }
  memset(c, 0, sizeof *c);
  return c;
}

static void candidate_unref_icon(BrCandidate *c) {
  if (c->kind != BR_CAND_APP && c->icon) {
    g_object_unref(c->icon);
    c->icon = NULL;
  }
}

static void candidates_clear(AppContext *ctx) {
  if (!ctx->candidates) {
    return;
  }
  for (guint i = 0; i < ctx->candidates->len; i++) {
    candidate_unref_icon(g_ptr_array_index(ctx->candidates, i));
  }
  br_arena_reset(&ctx->candidate_arena);
  g_ptr_array_set_size(ctx->candidates, 0);
}

static void clamp_selected(AppContext *ctx) {
  int n = (int)ctx->candidates->len;
  if (n <= 0) {
    ctx->selected = 0;
    return;
  }
  if (ctx->selected < 0) {
    ctx->selected = 0;
  }
  if (ctx->selected >= n) {
    ctx->selected = n - 1;
  }
}

static void add_bang_rows(AppContext *ctx, const char *kw, const char *tail) {
  if (!kw) {
    return;
  }
  if (!*kw) {
    GHashTableIter it;
    gpointer gk, gv;
    g_hash_table_iter_init(&it, ctx->config.bangs);
    int cap = ctx->config.max_visible_rows;
    int n = 0;
    while (g_hash_table_iter_next(&it, &gk, &gv) && n < cap) {
      BrCandidate *c = alloc_candidate(ctx);
      if (!c) {
        break;
      }
      c->kind = BR_CAND_BANG;
      g_autofree gchar *t = g_strdup_printf("!%s", (char *)gk);
      c->title = br_arena_strdup(&ctx->candidate_arena, t);
      c->subtitle = br_arena_strdup(&ctx->candidate_arena, (char *)gv);
      c->icon = ctx->icon_bang ? g_object_ref(ctx->icon_bang) : NULL;
      g_autofree gchar *ou = br_bang_build_url((char *)gv, "");
      c->open_uri = br_arena_strdup(&ctx->candidate_arena, ou);
      g_ptr_array_add(ctx->candidates, c);
      n++;
    }
    return;
  }
  const char *tpl = g_hash_table_lookup(ctx->config.bangs, kw);
  if (!tpl) {
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      return;
    }
    c->kind = BR_CAND_BANG;
    c->title = br_arena_strdup(&ctx->candidate_arena, "Unknown bang");
    c->subtitle = br_arena_strdup(&ctx->candidate_arena, kw);
    c->icon = ctx->icon_bang ? g_object_ref(ctx->icon_bang) : NULL;
    g_ptr_array_add(ctx->candidates, c);
    return;
  }
  g_autofree gchar *uri = br_bang_build_url(tpl, tail);
  BrCandidate *c = alloc_candidate(ctx);
  if (!c) {
    return;
  }
  c->kind = BR_CAND_BANG;
  g_autofree gchar *title = g_strdup_printf("!%s", kw);
  c->title = br_arena_strdup(&ctx->candidate_arena, title);
  c->subtitle = br_arena_strdup(
      &ctx->candidate_arena, tail && *tail ? tail : uri);
  c->icon = ctx->icon_bang ? g_object_ref(ctx->icon_bang) : NULL;
  c->open_uri = br_arena_strdup(&ctx->candidate_arena, uri);
  g_ptr_array_add(ctx->candidates, c);
}

static void add_app_and_file_rows(AppContext *ctx) {
  gint *idx = NULL;
  int nidx = 0;
  apps_filter_indices(ctx->apps, ctx->query, &idx, &nidx);
  int cap = ctx->config.max_visible_rows;
  int used = 0;
  for (int i = 0; i < nidx && used < cap; i++) {
    AppEntry *e = g_ptr_array_index(ctx->apps, (guint)idx[i]);
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      break;
    }
    c->kind = BR_CAND_APP;
    c->title = br_arena_strdup(
        &ctx->candidate_arena, g_app_info_get_display_name(G_APP_INFO(e->info)));
    c->subtitle =
        br_arena_strdup(&ctx->candidate_arena, g_app_info_get_name(G_APP_INFO(e->info)));
    c->icon = e->icon;
    c->app_index = (guint)idx[i];
    g_ptr_array_add(ctx->candidates, c);
    used++;
  }
  g_free(idx);

  pthread_mutex_lock(&ctx->file_search.mx);
  for (guint i = 0; i < ctx->file_search.paths->len && used < cap; i++) {
    const char *path = g_ptr_array_index(ctx->file_search.paths, i);
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      break;
    }
    c->kind = BR_CAND_FILE;
    g_autofree gchar *base = g_path_get_basename(path);
    c->title = br_arena_strdup(&ctx->candidate_arena, base);
    c->subtitle = br_arena_strdup(&ctx->candidate_arena, path);
    c->icon = ctx->icon_file ? g_object_ref(ctx->icon_file) : NULL;
    c->file_path = br_arena_strdup(&ctx->candidate_arena, path);
    g_ptr_array_add(ctx->candidates, c);
    used++;
  }
  pthread_mutex_unlock(&ctx->file_search.mx);
}

void br_ctx_refilter(AppContext *ctx) {
  if (!ctx->candidates) {
    ctx->candidates = g_ptr_array_new();
  }
  candidates_clear(ctx);

  g_autofree gchar *kw = NULL;
  g_autofree gchar *tail = NULL;
  if (br_bang_parse(ctx->query, &ctx->config, &kw, &tail)) {
    add_bang_rows(ctx, kw, tail);
  } else {
    add_app_and_file_rows(ctx);
  }
  clamp_selected(ctx);
  ctx->list_first_visible = 0;
  ctx->list_recenter_px = 0;
  ctx->list_scroll_interact_ms = 0;
  if (ctx->surf_width > 0 && ctx->surf_height > 0) {
    bookrunner_list_ensure_scroll(ctx, ctx->surf_width, ctx->surf_height);
  }
  ctx->needs_draw = true;
}

void br_ctx_select_move(AppContext *ctx, int delta) {
  if (!ctx->candidates) {
    return;
  }
  ctx->selected += delta;
  clamp_selected(ctx);
  ctx->list_scroll_anim_px -= (double)delta * 9.0;
  if (ctx->list_scroll_anim_px > 20.0) {
    ctx->list_scroll_anim_px = 20.0;
  }
  if (ctx->list_scroll_anim_px < -20.0) {
    ctx->list_scroll_anim_px = -20.0;
  }
  ctx->sel_pulse = 1.0;
  ctx->list_anim_last_ms = 0;
  ctx->list_scroll_interact_ms = bookrunner_mono_ms();
  if (ctx->surf_width > 0 && ctx->surf_height > 0) {
    bookrunner_list_ensure_scroll(ctx, ctx->surf_width, ctx->surf_height);
  }
  ctx->needs_draw = true;
}

BrCandidate *br_ctx_selected(const AppContext *ctx) {
  if (!ctx->candidates || ctx->candidates->len == 0) {
    return NULL;
  }
  if (ctx->selected < 0 || ctx->selected >= (int)ctx->candidates->len) {
    return NULL;
  }
  return g_ptr_array_index(ctx->candidates, (guint)ctx->selected);
}

void br_ctx_activate(AppContext *ctx) {
  BrCandidate *c = br_ctx_selected(ctx);
  if (!c) {
    return;
  }
  if (c->kind == BR_CAND_BANG && (!c->open_uri || !c->open_uri[0])) {
    return;
  }
  br_ctx_free_launch_fields(ctx);
  if (c->kind == BR_CAND_APP) {
    AppEntry *e = g_ptr_array_index(ctx->apps, c->app_index);
    ctx->launch_app = g_object_ref(e->info);
  } else if (c->kind == BR_CAND_BANG) {
    if (c->open_uri) {
      ctx->launch_uri = g_strdup(c->open_uri);
    }
  } else if (c->kind == BR_CAND_FILE && c->file_path) {
    ctx->launch_file = g_strdup(c->file_path);
  }
  ctx->done = true;
  ctx->exit_code = 0;
}

void br_ctx_free_launch_fields(AppContext *ctx) {
  g_clear_object(&ctx->launch_app);
  g_free(ctx->launch_uri);
  ctx->launch_uri = NULL;
  g_free(ctx->launch_file);
  ctx->launch_file = NULL;
}

void br_ctx_candidates_clear(AppContext *ctx) {
  candidates_clear(ctx);
}
