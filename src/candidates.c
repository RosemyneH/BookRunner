#include "candidates.h"

#include "apps.h"
#include "bangs.h"
#include "br_state.h"
#include "context.h"
#include "render.h"
#include "usage_db.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  const GPtrArray *apps;
  BrUsageDb *usage;
} BrAppIdxSortCtx;

static int cmp_app_indices(const void *a, const void *b, void *userdata) {
  gint ia = *(const gint *)a;
  gint ib = *(const gint *)b;
  BrAppIdxSortCtx *s = userdata;
  AppEntry *ea = g_ptr_array_index(s->apps, (guint)ia);
  AppEntry *eb = g_ptr_array_index(s->apps, (guint)ib);
  const char *ida = g_app_info_get_id(G_APP_INFO(ea->info));
  const char *idb = g_app_info_get_id(G_APP_INFO(eb->info));
  gint64 ca = br_usage_get(s->usage, ida);
  gint64 cb = br_usage_get(s->usage, idb);
  if (ca != cb) {
    return (ca > cb) ? -1 : 1;
  }
  const char *na = g_app_info_get_display_name(G_APP_INFO(ea->info));
  const char *nb = g_app_info_get_display_name(G_APP_INFO(eb->info));
  return g_utf8_collate(na ? na : "", nb ? nb : "");
}

static int cmp_cstr(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static const char *bang_kw_label(const char *kw) {
  if (!kw || !kw[0]) {
    return "Bang";
  }
  if (strcmp(kw, "g") == 0) {
    return "Google";
  }
  if (strcmp(kw, "w") == 0) {
    return "Wikipedia";
  }
  if (strcmp(kw, "yt") == 0) {
    return "YouTube";
  }
  if (strcmp(kw, "f") == 0) {
    return "Files";
  }
  if (strcmp(kw, "set") == 0) {
    return "Settings";
  }
  return kw;
}

static const char *bang_row_label(AppContext *ctx, const char *kw) {
  if (!kw || !kw[0]) {
    return "Bang";
  }
  const char *d = g_hash_table_lookup(ctx->config.bang_desc, kw);
  if (d && d[0]) {
    return d;
  }
  return bang_kw_label(kw);
}

static const char *bang_kw_icon_name(const char *kw) {
  if (!kw) {
    return "web-browser";
  }
  if (strcmp(kw, "g") == 0) {
    return "google";
  }
  if (strcmp(kw, "w") == 0) {
    return "wikipedia";
  }
  if (strcmp(kw, "yt") == 0) {
    return "youtube";
  }
  if (strcmp(kw, "f") == 0) {
    return "folder";
  }
  if (strcmp(kw, "set") == 0) {
    return "preferences-desktop";
  }
  return "applications-internet";
}

static void bang_row_set_icon(BrCandidate *c, const char *kw) {
  GdkPixbuf *pb = br_icon_from_theme(bang_kw_icon_name(kw), 36);
  c->icon = pb;
}

static void row_icon_theme(BrCandidate *c, const char *icon_name) {
  c->icon = br_icon_from_theme(icon_name, 36);
}

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

static void wrap_selected(AppContext *ctx) {
  int n = (int)ctx->candidates->len;
  if (n <= 0) {
    ctx->selected = 0;
    return;
  }
  while (ctx->selected < 0) {
    ctx->selected += n;
  }
  while (ctx->selected >= n) {
    ctx->selected -= n;
  }
}

static void append_file_rows(AppContext *ctx, int cap, int *used) {
  pthread_mutex_lock(&ctx->file_search.mx);
  for (guint i = 0; i < ctx->file_search.paths->len && *used < cap; i++) {
    const char *path = g_ptr_array_index(ctx->file_search.paths, i);
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      break;
    }
    c->kind = BR_CAND_FILE;
    g_autofree gchar *base = g_path_get_basename(path);
    g_autofree gchar *line = g_strdup_printf("Open > %s", base);
    c->title = br_arena_strdup(&ctx->candidate_arena, line);
    c->subtitle = NULL;
    c->icon = ctx->icon_file ? g_object_ref(ctx->icon_file) : NULL;
    c->file_path = br_arena_strdup(&ctx->candidate_arena, path);
    g_ptr_array_add(ctx->candidates, c);
    (*used)++;
  }
  pthread_mutex_unlock(&ctx->file_search.mx);
}

static void add_f_rows(AppContext *ctx, const char *tail) {
  int cap = ctx->config.max_visible_rows;
  int used = 0;
  if (!tail || !*tail) {
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      return;
    }
    c->kind = BR_CAND_BANG;
    c->title = br_arena_strdup(&ctx->candidate_arena, "Files >");
    c->subtitle = NULL;
    c->open_uri = NULL;
    bang_row_set_icon(c, "f");
    g_ptr_array_add(ctx->candidates, c);
    used++;
    append_file_rows(ctx, cap, &used);
    return;
  }
  append_file_rows(ctx, cap, &used);
}

static void add_set_help_bang_row(AppContext *ctx, const char *kw, const char *tpl, int cap, int *used) {
  (void)tpl;
  if (*used >= cap) {
    return;
  }
  BrCandidate *c = alloc_candidate(ctx);
  if (!c) {
    return;
  }
  c->kind = BR_CAND_BANG;
  c->title = br_arena_strdup(&ctx->candidate_arena, bang_row_label(ctx, kw));
  c->subtitle = NULL;
  c->bang_kw = br_arena_strdup(&ctx->candidate_arena, kw);
  bang_row_set_icon(c, kw);
  c->open_uri = NULL;
  g_ptr_array_add(ctx->candidates, c);
  (*used)++;
}

static void add_set_rows(AppContext *ctx) {
  int cap = ctx->config.max_visible_rows;
  int used = 0;
  BrCandidate *h = alloc_candidate(ctx);
  if (!h) {
    return;
  }
  h->kind = BR_CAND_BANG;
  h->title = br_arena_strdup(&ctx->candidate_arena, "Settings");
  h->subtitle = NULL;
  h->open_uri = NULL;
  row_icon_theme(h, "preferences-desktop");
  g_ptr_array_add(ctx->candidates, h);
  used++;

  BrCandidate *t = alloc_candidate(ctx);
  if (!t) {
    return;
  }
  t->kind = BR_CAND_ACTION;
  t->act = BR_ACT_TOGGLE_BANG_F;
  g_autofree gchar *ft = g_strdup_printf(
      "Files > %s", ctx->config.bang_f_enabled ? "on" : "off");
  t->title = br_arena_strdup(&ctx->candidate_arena, ft);
  t->subtitle = NULL;
  bang_row_set_icon(t, "f");
  g_ptr_array_add(ctx->candidates, t);
  used++;

  BrCandidate *sec = alloc_candidate(ctx);
  if (!sec) {
    return;
  }
  sec->kind = BR_CAND_BANG;
  sec->title = br_arena_strdup(&ctx->candidate_arena, "Ignored");
  sec->subtitle = NULL;
  sec->open_uri = NULL;
  row_icon_theme(sec, "user-trash");
  g_ptr_array_add(ctx->candidates, sec);
  used++;

  if (g_hash_table_size(ctx->config.ignored_apps) == 0) {
    BrCandidate *e = alloc_candidate(ctx);
    if (e) {
      e->kind = BR_CAND_BANG;
      e->title = br_arena_strdup(&ctx->candidate_arena, "(none)");
      e->subtitle = NULL;
      e->open_uri = NULL;
      row_icon_theme(e, "dialog-question");
      g_ptr_array_add(ctx->candidates, e);
      used++;
    }
  } else {
    GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
    GHashTableIter it;
    gpointer k, v;
    g_hash_table_iter_init(&it, ctx->config.ignored_apps);
    while (g_hash_table_iter_next(&it, &k, &v)) {
      (void)v;
      g_ptr_array_add(ids, g_strdup((char *)k));
    }
    g_ptr_array_sort(ids, cmp_cstr);
    for (guint i = 0; i < ids->len && used < cap; i++) {
      const char *did = g_ptr_array_index(ids, i);
      const char *dname = did;
      for (guint j = 0; j < ctx->apps->len; j++) {
        AppEntry *ae = g_ptr_array_index(ctx->apps, j);
        const char *id = g_app_info_get_id(G_APP_INFO(ae->info));
        if (id && strcmp(id, did) == 0) {
          dname = g_app_info_get_display_name(G_APP_INFO(ae->info));
          break;
        }
      }
      BrCandidate *row = alloc_candidate(ctx);
      if (!row) {
        break;
      }
      row->kind = BR_CAND_ACTION;
      row->act = BR_ACT_UNIGNORE;
      row->action_id = br_arena_strdup(&ctx->candidate_arena, did);
      g_autofree gchar *rt = g_strdup_printf("%s > restore", dname);
      row->title = br_arena_strdup(&ctx->candidate_arena, rt);
      row->subtitle = NULL;
      row_icon_theme(row, "edit-undo");
      g_ptr_array_add(ctx->candidates, row);
      used++;
    }
    g_ptr_array_unref(ids);
  }

  BrCandidate *bsec = alloc_candidate(ctx);
  if (!bsec) {
    return;
  }
  bsec->kind = BR_CAND_BANG;
  bsec->title = br_arena_strdup(&ctx->candidate_arena, "Commands");
  bsec->subtitle = NULL;
  bsec->open_uri = NULL;
  row_icon_theme(bsec, "help-browser");
  g_ptr_array_add(ctx->candidates, bsec);
  used++;

  add_set_help_bang_row(ctx, "set", "", cap, &used);
  if (ctx->config.bang_f_enabled && !g_hash_table_contains(ctx->config.bangs, "f")) {
    add_set_help_bang_row(ctx, "f", "", cap, &used);
  }
  add_set_help_bang_row(ctx, "g", "https://www.google.com/search?q=%s", cap, &used);
  add_set_help_bang_row(ctx, "w", "https://en.wikipedia.org/wiki/Special:Search?search=%s", cap, &used);
  add_set_help_bang_row(ctx, "yt", "https://www.youtube.com/results?search_query=%s", cap, &used);

  GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
  GHashTableIter bit;
  gpointer gk, gv;
  g_hash_table_iter_init(&bit, ctx->config.bangs);
  while (g_hash_table_iter_next(&bit, &gk, &gv)) {
    const char *key = (const char *)gk;
    if (strcmp(key, "g") == 0 || strcmp(key, "w") == 0 || strcmp(key, "yt") == 0) {
      continue;
    }
    if (!ctx->config.bang_f_enabled && strcmp(key, "f") == 0) {
      continue;
    }
    g_ptr_array_add(keys, g_strdup(key));
  }
  g_ptr_array_sort(keys, cmp_cstr);
  for (guint i = 0; i < keys->len && used < cap; i++) {
    const char *key = g_ptr_array_index(keys, i);
    if (strcmp(key, "set") == 0) {
      continue;
    }
    const char *tpl = g_hash_table_lookup(ctx->config.bangs, key);
    add_set_help_bang_row(ctx, key, tpl ? tpl : "", cap, &used);
  }
  g_ptr_array_unref(keys);
}

static void add_bang_prefix_rows(AppContext *ctx, const char *kw) {
  GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
  GHashTableIter it;
  gpointer gk, gv;
  g_hash_table_iter_init(&it, ctx->config.bangs);
  while (g_hash_table_iter_next(&it, &gk, &gv)) {
    const char *key = (const char *)gk;
    const char *val = (const char *)gv;
    if (strcmp(key, "set") == 0) {
      continue;
    }
    if (!ctx->config.bang_f_enabled && strcmp(key, "f") == 0) {
      continue;
    }
    if (strcmp(key, "f") == 0 && ctx->config.bang_f_enabled && (!val || !val[0])) {
      /* allow synthetic f */
    } else if (!val || !val[0]) {
      continue;
    }
    if (!g_str_has_prefix(key, kw)) {
      continue;
    }
    g_ptr_array_add(keys, g_strdup(key));
  }
  g_ptr_array_sort(keys, cmp_cstr);
  int cap = ctx->config.max_visible_rows;
  if (keys->len == 0) {
    g_ptr_array_unref(keys);
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      return;
    }
    c->kind = BR_CAND_BANG;
    g_autofree gchar *unk = g_strdup_printf("Unknown > %s", kw);
    c->title = br_arena_strdup(&ctx->candidate_arena, unk);
    c->subtitle = NULL;
    row_icon_theme(c, "dialog-warning");
    g_ptr_array_add(ctx->candidates, c);
    return;
  }
  int n = 0;
  for (guint i = 0; i < keys->len && n < cap; i++) {
    const char *key = g_ptr_array_index(keys, i);
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      break;
    }
    c->kind = BR_CAND_BANG;
    c->title = br_arena_strdup(&ctx->candidate_arena, bang_row_label(ctx, key));
    c->subtitle = NULL;
    c->bang_kw = br_arena_strdup(&ctx->candidate_arena, key);
    bang_row_set_icon(c, key);
    c->open_uri = NULL;
    g_ptr_array_add(ctx->candidates, c);
    n++;
  }
  g_ptr_array_unref(keys);
}

static void add_bang_rows(AppContext *ctx, const char *kw, const char *tail) {
  if (!kw) {
    return;
  }
  if (strcmp(kw, "set") == 0) {
    add_set_rows(ctx);
    return;
  }
  if (ctx->config.bang_f_enabled && strcmp(kw, "f") == 0) {
    add_f_rows(ctx, tail);
    return;
  }
  if (!*kw) {
    GPtrArray *keys = g_ptr_array_new_with_free_func(g_free);
    GHashTableIter it;
    gpointer gk, gv;
    g_hash_table_iter_init(&it, ctx->config.bangs);
    while (g_hash_table_iter_next(&it, &gk, &gv)) {
      const char *key = (const char *)gk;
      if (!ctx->config.bang_f_enabled && strcmp(key, "f") == 0) {
        continue;
      }
      g_ptr_array_add(keys, g_strdup(key));
    }
    if (ctx->config.bang_f_enabled && !g_hash_table_contains(ctx->config.bangs, "f")) {
      g_ptr_array_add(keys, g_strdup("f"));
    }
    if (!g_hash_table_contains(ctx->config.bangs, "set")) {
      g_ptr_array_add(keys, g_strdup("set"));
    }
    g_ptr_array_sort(keys, cmp_cstr);
    int cap = ctx->config.max_visible_rows;
    int n = 0;
    for (guint i = 0; i < keys->len && n < cap; i++) {
      const char *key = g_ptr_array_index(keys, i);
      const char *val = g_hash_table_lookup(ctx->config.bangs, key);
      if (strcmp(key, "f") == 0 && ctx->config.bang_f_enabled && !val) {
        val = "";
      }
      if (!val) {
        continue;
      }
      BrCandidate *c = alloc_candidate(ctx);
      if (!c) {
        break;
      }
      c->kind = BR_CAND_BANG;
      c->title = br_arena_strdup(&ctx->candidate_arena, bang_row_label(ctx, key));
      c->subtitle = NULL;
      c->bang_kw = br_arena_strdup(&ctx->candidate_arena, key);
      bang_row_set_icon(c, key);
      c->open_uri = NULL;
      g_ptr_array_add(ctx->candidates, c);
      n++;
    }
    g_ptr_array_unref(keys);
    return;
  }
  const char *tpl = g_hash_table_lookup(ctx->config.bangs, kw);
  if (!tpl) {
    add_bang_prefix_rows(ctx, kw);
    return;
  }
  if (!tail || !*tail) {
    BrCandidate *c = alloc_candidate(ctx);
    if (!c) {
      return;
    }
    c->kind = BR_CAND_BANG;
    const char *lab = bang_row_label(ctx, kw);
    c->title = br_arena_strdup(&ctx->candidate_arena, lab);
    c->subtitle = NULL;
    c->bang_kw = br_arena_strdup(&ctx->candidate_arena, kw);
    bang_row_set_icon(c, kw);
    c->open_uri = NULL;
    g_ptr_array_add(ctx->candidates, c);
    return;
  }
  g_autofree gchar *uri = br_bang_build_url(tpl, tail);
  BrCandidate *c = alloc_candidate(ctx);
  if (!c) {
    return;
  }
  c->kind = BR_CAND_BANG;
  const char *lab = bang_row_label(ctx, kw);
  g_autofree gchar *line = g_strdup_printf("%s > %s", lab, tail);
  c->title = br_arena_strdup(&ctx->candidate_arena, line);
  c->subtitle = NULL;
  bang_row_set_icon(c, kw);
  c->open_uri = br_arena_strdup(&ctx->candidate_arena, uri);
  g_ptr_array_add(ctx->candidates, c);
}

static void add_app_and_file_rows(AppContext *ctx) {
  gint *idx = NULL;
  int nidx = 0;
  apps_filter_indices(ctx->apps, ctx->query, &ctx->config, &idx, &nidx);
  BrAppIdxSortCtx sctx = {.apps = ctx->apps, .usage = ctx->usage_db};
  if (nidx > 1) {
    qsort_r(idx, (size_t)nidx, sizeof idx[0], cmp_app_indices, &sctx);
  }
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
    c->subtitle = NULL;
    c->app_index = (guint)idx[i];
    g_ptr_array_add(ctx->candidates, c);
    used++;
  }
  g_free(idx);

  append_file_rows(ctx, cap, &used);
}

void br_ctx_refilter(AppContext *ctx) {
  if (!ctx->candidates) {
    ctx->candidates = g_ptr_array_new();
  }
  candidates_clear(ctx);

  if (ctx->bang_follow_kw[0]) {
    if (ctx->config.bang_f_enabled && strcmp(ctx->bang_follow_kw, "f") == 0) {
      add_f_rows(ctx, ctx->bang_follow_q);
    }
    clamp_selected(ctx);
    ctx->list_scroll_init_done = false;
    ctx->list_scroll_interact_ms = 0;
    ctx->needs_draw = true;
    return;
  }

  g_autofree gchar *kw = NULL;
  g_autofree gchar *tail = NULL;
  if (br_bang_parse(ctx->query, &ctx->config, &kw, &tail)) {
    g_autofree gchar *kw_fold = g_utf8_strdown(kw, -1);
    add_bang_rows(ctx, kw_fold, tail);
  } else {
    add_app_and_file_rows(ctx);
  }
  clamp_selected(ctx);
  ctx->list_scroll_init_done = false;
  ctx->list_scroll_interact_ms = 0;
  ctx->needs_draw = true;
}

void br_ctx_select_move(AppContext *ctx, int delta) {
  if (!ctx->candidates) {
    return;
  }
  int n = (int)ctx->candidates->len;
  if (n <= 0) {
    return;
  }
  int prev = ctx->selected;
  ctx->selected += delta;
  bool wrapped = false;
  if (ctx->config.list_wrap) {
    if (delta < 0 && prev == 0) {
      wrapped = true;
    } else if (delta > 0 && prev == n - 1) {
      wrapped = true;
    }
    wrap_selected(ctx);
  } else {
    clamp_selected(ctx);
  }
  if (wrapped) {
    ctx->list_scroll_init_done = false;
  }
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

bool br_ctx_bang_followup_active(const AppContext *ctx) {
  return ctx->bang_follow_kw[0] != '\0';
}

static void br_ctx_bang_followup_start(AppContext *ctx, const char *kw) {
  if (!kw || !kw[0]) {
    return;
  }
  g_clear_object(&ctx->bang_follow_icon);
  g_strlcpy(ctx->bang_follow_kw, kw, sizeof ctx->bang_follow_kw);
  const char *lab = bang_row_label(ctx, kw);
  g_strlcpy(ctx->bang_follow_label, lab, sizeof ctx->bang_follow_label);
  ctx->bang_follow_q[0] = '\0';
  g_strlcpy(ctx->bang_restore_query, ctx->query, sizeof ctx->bang_restore_query);
  ctx->query[0] = '\0';
  ctx->bang_follow_icon = br_icon_from_theme(bang_kw_icon_name(kw), 36);
  ctx->selected = 0;
  br_ctx_refilter(ctx);
  br_file_search_on_query_changed(ctx);
}

void br_ctx_bang_followup_cancel(AppContext *ctx) {
  if (!br_ctx_bang_followup_active(ctx)) {
    return;
  }
  g_strlcpy(ctx->query, ctx->bang_restore_query, sizeof ctx->query);
  g_clear_object(&ctx->bang_follow_icon);
  ctx->bang_follow_kw[0] = '\0';
  ctx->bang_follow_q[0] = '\0';
  ctx->bang_follow_label[0] = '\0';
  ctx->bang_restore_query[0] = '\0';
  br_ctx_refilter(ctx);
  br_file_search_on_query_changed(ctx);
}

void br_ctx_bang_followup_launch(AppContext *ctx) {
  if (!br_ctx_bang_followup_active(ctx)) {
    return;
  }
  if (strcmp(ctx->bang_follow_kw, "f") == 0) {
    return;
  }
  const char *tpl = g_hash_table_lookup(ctx->config.bangs, ctx->bang_follow_kw);
  if (!tpl || !tpl[0]) {
    return;
  }
  g_autofree gchar *uri = br_bang_build_url(tpl, ctx->bang_follow_q);
  if (!uri) {
    return;
  }
  br_ctx_free_launch_fields(ctx);
  ctx->launch_uri = g_strdup(uri);
  g_clear_object(&ctx->bang_follow_icon);
  ctx->bang_follow_kw[0] = '\0';
  ctx->bang_follow_q[0] = '\0';
  ctx->bang_follow_label[0] = '\0';
  ctx->bang_restore_query[0] = '\0';
  ctx->done = true;
  ctx->exit_code = 0;
}

void br_ctx_submit(AppContext *ctx) {
  if (br_ctx_bang_followup_active(ctx)) {
    BrCandidate *c = br_ctx_selected(ctx);
    if (c && ((c->kind == BR_CAND_FILE && c->file_path) || c->kind == BR_CAND_APP ||
              (c->kind == BR_CAND_BANG && c->open_uri && c->open_uri[0]))) {
      br_ctx_activate(ctx);
      return;
    }
    br_ctx_bang_followup_launch(ctx);
    return;
  }
  br_ctx_activate(ctx);
}

void br_ctx_activate(AppContext *ctx) {
  BrCandidate *c = br_ctx_selected(ctx);
  if (!c) {
    return;
  }
  if (c->kind == BR_CAND_ACTION) {
    if (c->act == BR_ACT_TOGGLE_BANG_F) {
      ctx->config.bang_f_enabled = !ctx->config.bang_f_enabled;
      br_state_save(&ctx->config);
      br_ctx_refilter(ctx);
      br_file_search_on_query_changed(ctx);
      return;
    }
    if (c->act == BR_ACT_UNIGNORE && c->action_id) {
      g_hash_table_remove(ctx->config.ignored_apps, c->action_id);
      br_state_save(&ctx->config);
      br_ctx_refilter(ctx);
      br_file_search_on_query_changed(ctx);
      return;
    }
    return;
  }
  if (c->kind == BR_CAND_BANG) {
    if (c->open_uri && c->open_uri[0]) {
      br_ctx_free_launch_fields(ctx);
      ctx->launch_uri = g_strdup(c->open_uri);
      ctx->done = true;
      ctx->exit_code = 0;
      return;
    }
    if (c->bang_kw && c->bang_kw[0]) {
      br_ctx_bang_followup_start(ctx, c->bang_kw);
      return;
    }
    return;
  }
  br_ctx_free_launch_fields(ctx);
  if (c->kind == BR_CAND_APP) {
    AppEntry *e = g_ptr_array_index(ctx->apps, c->app_index);
    ctx->launch_app = g_object_ref(e->info);
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

bool br_ctx_ignore_selected_app(AppContext *ctx) {
  BrCandidate *c = br_ctx_selected(ctx);
  if (!c || c->kind != BR_CAND_APP) {
    return false;
  }
  AppEntry *e = g_ptr_array_index(ctx->apps, c->app_index);
  const char *id = g_app_info_get_id(G_APP_INFO(e->info));
  if (!id || !id[0]) {
    return false;
  }
  g_hash_table_insert(ctx->config.ignored_apps, g_strdup(id), (gpointer)1);
  br_state_save(&ctx->config);
  br_ctx_refilter(ctx);
  br_file_search_on_query_changed(ctx);
  return true;
}
