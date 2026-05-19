#include "br_state.h"

#include <glib.h>

void br_state_load(BrConfig *cfg) {
  g_autofree gchar *dir = g_build_filename(g_get_user_data_dir(), "bookrunner", NULL);
  g_autofree gchar *path = g_build_filename(dir, "state.ini", NULL);
  g_autoptr(GKeyFile) kf = g_key_file_new();
  if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
    return;
  }
  GError *err = NULL;
  gboolean bf = g_key_file_get_boolean(kf, "settings", "bang_f_enabled", &err);
  if (!err) {
    cfg->bang_f_enabled = bf;
  }
  g_clear_error(&err);
  gboolean fs = g_key_file_get_boolean(kf, "settings", "file_search_enabled", &err);
  if (!err) {
    cfg->file_search_enabled = fs;
  }
  g_clear_error(&err);
  g_hash_table_remove_all(cfg->ignored_apps);
  if (g_key_file_has_group(kf, "ignore")) {
    gsize n = 0;
    gchar **keys = g_key_file_get_keys(kf, "ignore", &n, NULL);
    if (keys) {
      for (gsize i = 0; i < n; i++) {
        g_hash_table_insert(cfg->ignored_apps, g_strdup(keys[i]), (gpointer)1);
      }
      g_strfreev(keys);
    }
  }
}

void br_state_save(const BrConfig *cfg) {
  g_autofree gchar *dir = g_build_filename(g_get_user_data_dir(), "bookrunner", NULL);
  if (g_mkdir_with_parents(dir, 0755) < 0) {
    return;
  }
  g_autofree gchar *path = g_build_filename(dir, "state.ini", NULL);
  g_autoptr(GKeyFile) kf = g_key_file_new();
  g_key_file_set_boolean(kf, "settings", "bang_f_enabled", cfg->bang_f_enabled);
  g_key_file_set_boolean(kf, "settings", "file_search_enabled", cfg->file_search_enabled);
  GHashTableIter it;
  gpointer k, v;
  g_hash_table_iter_init(&it, cfg->ignored_apps);
  while (g_hash_table_iter_next(&it, &k, &v)) {
    (void)v;
    g_key_file_set_string(kf, "ignore", (char *)k, "1");
  }
  g_key_file_save_to_file(kf, path, NULL);
}
