#include "config.h"

#include <stdlib.h>
#include <string.h>

static uint32_t parse_color(const char *s, uint32_t def) {
  if (!s || !*s) {
    return def;
  }
  const char *p = s;
  if (p[0] == '#') {
    p++;
  }
  char *end = NULL;
  unsigned long v = strtoul(p, &end, 16);
  if (end == p) {
    return def;
  }
  if (strlen(p) <= 6) {
    return (uint32_t)((0xffu << 24) | (v & 0xffffffu));
  }
  return (uint32_t)(v & 0xffffffffu);
}

static BrFileBackend parse_backend(const char *s) {
  if (!s) {
    return BR_FILE_BACKEND_AUTO;
  }
  if (g_ascii_strcasecmp(s, "fd") == 0) {
    return BR_FILE_BACKEND_FD;
  }
  if (g_ascii_strcasecmp(s, "plocate") == 0 || g_ascii_strcasecmp(s, "locate") == 0) {
    return BR_FILE_BACKEND_PLOCATE;
  }
  return BR_FILE_BACKEND_AUTO;
}

static GStrv split_roots(const char *s) {
  if (!s || !*s) {
    return g_strdupv((char *[]){g_strdup(g_get_home_dir()), NULL});
  }
  gchar **parts = g_strsplit(s, ",", 0);
  GPtrArray *a = g_ptr_array_new();
  for (gchar **p = parts; *p; p++) {
    g_strstrip(*p);
    if (!**p) {
      continue;
    }
    char *expanded = NULL;
    if ((*p)[0] == '~' && (*p)[1] == '/') {
      expanded = g_build_filename(g_get_home_dir(), *p + 2, NULL);
    } else if (g_str_has_prefix(*p, "$HOME")) {
      expanded = g_strconcat(g_get_home_dir(), *p + 5, NULL);
    } else {
      expanded = g_strdup(*p);
    }
    g_ptr_array_add(a, expanded);
  }
  g_strfreev(parts);
  if (a->len == 0) {
    g_ptr_array_add(a, g_strdup(g_get_home_dir()));
  }
  g_ptr_array_add(a, NULL);
  return (GStrv)g_ptr_array_free(a, FALSE);
}

void br_config_init_defaults(BrConfig *c) {
  memset(c, 0, sizeof(*c));
  c->ui_width = 380;
  c->ui_height = 480;
  c->font = g_strdup("Sans");
  c->font_size = 14.0;
  c->bang_prefix = g_strdup("!");
  c->max_visible_rows = 512;
  c->debounce_ms = 160;
  c->file_timeout_ms = 8000;
  c->max_file_results = 24;
  c->file_backend = BR_FILE_BACKEND_AUTO;
  c->fd_command = g_strdup("fd");
  c->plocate_command = g_strdup("plocate");
  c->file_roots = g_strdupv((char *[]){g_strdup(g_get_home_dir()), NULL});
  c->col_panel = 0xdd1a1520u;
  c->col_input_bg = 0xf0101010u;
  c->col_text = 0xffffffffu;
  c->col_dim = 0xccaaaaaau;
  c->col_border = 0x88444444u;
  c->col_row_sel = 0x55333355u;
  c->list_wrap = true;
  c->invert_list_wheel = true;
  c->bangs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  c->bang_desc = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  c->bang_icons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  c->ignored_apps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  c->bang_f_enabled = FALSE;
}

static void merge_bang_desc(GHashTable *into, GKeyFile *kf) {
  if (!g_key_file_has_group(kf, "bang_desc")) {
    return;
  }
  gsize n = 0;
  gchar **keys = g_key_file_get_keys(kf, "bang_desc", &n, NULL);
  if (!keys) {
    return;
  }
  for (gsize i = 0; i < n; i++) {
    gchar *v = g_key_file_get_string(kf, "bang_desc", keys[i], NULL);
    if (v) {
      g_hash_table_insert(into, g_strdup(keys[i]), v);
    }
  }
  g_strfreev(keys);
}

static void merge_bang_icons(GHashTable *into, GKeyFile *kf) {
  if (!g_key_file_has_group(kf, "bang_icons")) {
    return;
  }
  gsize n = 0;
  gchar **keys = g_key_file_get_keys(kf, "bang_icons", &n, NULL);
  if (!keys) {
    return;
  }
  for (gsize i = 0; i < n; i++) {
    gchar *v = g_key_file_get_string(kf, "bang_icons", keys[i], NULL);
    if (v) {
      g_hash_table_insert(into, g_strdup(keys[i]), v);
    }
  }
  g_strfreev(keys);
}

void br_config_clear(BrConfig *c) {
  g_free(c->font);
  g_free(c->bang_prefix);
  g_free(c->fd_command);
  g_free(c->plocate_command);
  g_strfreev(c->file_roots);
  if (c->bangs) {
    g_hash_table_destroy(c->bangs);
  }
  if (c->bang_desc) {
    g_hash_table_destroy(c->bang_desc);
  }
  if (c->bang_icons) {
    g_hash_table_destroy(c->bang_icons);
  }
  if (c->ignored_apps) {
    g_hash_table_destroy(c->ignored_apps);
  }
  memset(c, 0, sizeof(*c));
}

static void merge_bangs(GHashTable *into, GKeyFile *kf) {
  if (!g_key_file_has_group(kf, "bangs")) {
    return;
  }
  gsize n = 0;
  gchar **keys = g_key_file_get_keys(kf, "bangs", &n, NULL);
  if (!keys) {
    return;
  }
  for (gsize i = 0; i < n; i++) {
    gchar *v = g_key_file_get_string(kf, "bangs", keys[i], NULL);
    if (v) {
      g_hash_table_insert(into, g_strdup(keys[i]), v);
    }
  }
  g_strfreev(keys);
}

void br_config_merge_bangs_ini(BrConfig *c, const char *path) {
  g_autoptr(GKeyFile) kf = g_key_file_new();
  if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
    return;
  }
  merge_bangs(c->bangs, kf);
  merge_bang_desc(c->bang_desc, kf);
  merge_bang_icons(c->bang_icons, kf);
}

void br_config_load(BrConfig *c) {
  br_config_init_defaults(c);
  {
    g_autofree gchar *pkg_bangs = g_build_filename(BR_PKGDATADIR, "bangs_generated.ini", NULL);
    br_config_merge_bangs_ini(c, pkg_bangs);
#if defined(BR_SRC_BANGS_INI)
    if (!g_file_test(pkg_bangs, G_FILE_TEST_IS_REGULAR)) {
      br_config_merge_bangs_ini(c, BR_SRC_BANGS_INI);
    }
#endif
  }
  if (g_getenv("BOOKRUNNER_BANGS_INI")) {
    br_config_merge_bangs_ini(c, g_getenv("BOOKRUNNER_BANGS_INI"));
  }
  {
    g_autofree gchar *data_bangs =
        g_build_filename(g_get_user_data_dir(), "bookrunner", "bangs_generated.ini", NULL);
    br_config_merge_bangs_ini(c, data_bangs);
  }
  g_autoptr(GKeyFile) kf = g_key_file_new();
  const gchar *user_path = g_build_filename(g_get_user_config_dir(), "bookrunner", "config.ini", NULL);
  const gchar *etc_path = "/etc/xdg/bookrunner/config.ini";
  GError *err = NULL;
  if (!g_key_file_load_from_file(kf, user_path, G_KEY_FILE_NONE, &err)) {
    g_clear_error(&err);
    if (!g_key_file_load_from_file(kf, etc_path, G_KEY_FILE_NONE, &err)) {
      g_clear_error(&err);
      return;
    }
  }

  if (g_key_file_has_group(kf, "ui")) {
    c->ui_width = (int)g_key_file_get_integer(kf, "ui", "width", NULL) ?: c->ui_width;
    c->ui_height = (int)g_key_file_get_integer(kf, "ui", "height", NULL) ?: c->ui_height;
    gchar *f = g_key_file_get_string(kf, "ui", "font", NULL);
    if (f) {
      g_free(c->font);
      c->font = f;
    }
    c->font_size = g_key_file_get_double(kf, "ui", "font_size", NULL) ?: c->font_size;
    gchar *bp = g_key_file_get_string(kf, "ui", "bang_prefix", NULL);
    if (bp) {
      g_free(c->bang_prefix);
      c->bang_prefix = bp;
    }
    c->max_visible_rows = (int)g_key_file_get_integer(kf, "ui", "max_visible_rows", NULL) ?: c->max_visible_rows;
    c->debounce_ms = (int)g_key_file_get_integer(kf, "ui", "debounce_ms", NULL) ?: c->debounce_ms;
    gchar *col = g_key_file_get_string(kf, "ui", "panel", NULL);
    c->col_panel = parse_color(col, c->col_panel);
    g_free(col);
    col = g_key_file_get_string(kf, "ui", "input_bg", NULL);
    c->col_input_bg = parse_color(col, c->col_input_bg);
    g_free(col);
    col = g_key_file_get_string(kf, "ui", "text", NULL);
    c->col_text = parse_color(col, c->col_text);
    g_free(col);
    col = g_key_file_get_string(kf, "ui", "dim", NULL);
    c->col_dim = parse_color(col, c->col_dim);
    g_free(col);
    col = g_key_file_get_string(kf, "ui", "border", NULL);
    c->col_border = parse_color(col, c->col_border);
    g_free(col);
    col = g_key_file_get_string(kf, "ui", "row_selected", NULL);
    c->col_row_sel = parse_color(col, c->col_row_sel);
    g_free(col);
    GError *be = NULL;
    gboolean lw = g_key_file_get_boolean(kf, "ui", "list_wrap", &be);
    if (!be) {
      c->list_wrap = lw;
    }
    g_clear_error(&be);
    gboolean inv = g_key_file_get_boolean(kf, "ui", "invert_list_wheel", &be);
    if (!be) {
      c->invert_list_wheel = inv;
    }
    g_clear_error(&be);
  }

  if (g_key_file_has_group(kf, "files")) {
    gchar *b = g_key_file_get_string(kf, "files", "backend", NULL);
    c->file_backend = parse_backend(b);
    g_free(b);
    gchar *fc = g_key_file_get_string(kf, "files", "fd_command", NULL);
    if (fc) {
      g_free(c->fd_command);
      c->fd_command = fc;
    }
    gchar *pc = g_key_file_get_string(kf, "files", "plocate_command", NULL);
    if (pc) {
      g_free(c->plocate_command);
      c->plocate_command = pc;
    }
    c->max_file_results = (int)g_key_file_get_integer(kf, "files", "max_file_results", NULL) ?: c->max_file_results;
    c->file_timeout_ms = (int)g_key_file_get_integer(kf, "files", "timeout_ms", NULL) ?: c->file_timeout_ms;
    gchar *roots = g_key_file_get_string(kf, "files", "roots", NULL);
    if (roots) {
      g_strfreev(c->file_roots);
      c->file_roots = split_roots(roots);
      g_free(roots);
    }
  }

  merge_bangs(c->bangs, kf);
  merge_bang_desc(c->bang_desc, kf);
  merge_bang_icons(c->bang_icons, kf);

  if (c->file_backend == BR_FILE_BACKEND_AUTO) {
    g_autofree gchar *fdp = g_find_program_in_path(c->fd_command);
    g_autofree gchar *fdf = g_find_program_in_path("fdfind");
    if (fdp || fdf) {
      c->file_backend = BR_FILE_BACKEND_FD;
    } else {
      g_autofree gchar *pl = g_find_program_in_path(c->plocate_command);
      if (!pl) {
        pl = g_find_program_in_path("locate");
      }
      c->file_backend = pl ? BR_FILE_BACKEND_PLOCATE : BR_FILE_BACKEND_FD;
    }
  }
}
