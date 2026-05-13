#include "apps.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <string.h>

GdkPixbuf *br_stub_icon(const char *hint, int size_px) {
  guint32 h = 5381;
  if (hint) {
    for (const guchar *p = (const guchar *)hint; *p; p++) {
      h = ((h << 5) + h) + *p;
    }
  }
  guint32 fill = 0xe0000000u | (h & 0xffffffu);
  GdkPixbuf *pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, size_px, size_px);
  if (!pix) {
    return NULL;
  }
  gdk_pixbuf_fill(pix, fill);
  return pix;
}

static char *br_read_icon_theme_from_settings(const char *subdir) {
  g_autofree gchar *path = g_build_filename(g_get_user_config_dir(), subdir, "settings.ini", NULL);
  if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
    return NULL;
  }
  GKeyFile *kf = g_key_file_new();
  if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
    g_key_file_free(kf);
    return NULL;
  }
  gchar *v = g_key_file_get_string(kf, "Settings", "gtk-icon-theme-name", NULL);
  g_key_file_free(kf);
  if (!v || !v[0]) {
    g_free(v);
    return NULL;
  }
  g_strstrip(v);
  size_t n = strlen(v);
  if (n >= 2 && v[0] == '"' && v[n - 1] == '"') {
    char *inner = g_strndup(v + 1, n - 2);
    g_free(v);
    return inner;
  }
  return v;
}

static char *br_read_user_icon_theme_name(void) {
  char *t = br_read_icon_theme_from_settings("gtk-4.0");
  if (t) {
    return t;
  }
  return br_read_icon_theme_from_settings("gtk-3.0");
}

static void br_icon_roots_collect(GPtrArray *roots) {
  const gchar *home = g_get_home_dir();
  g_ptr_array_add(roots, g_build_filename(home, ".local/share/icons", NULL));
  g_ptr_array_add(roots, g_build_filename(home, ".icons", NULL));
  const gchar *xdg = g_getenv("XDG_DATA_DIRS");
  if (xdg && xdg[0]) {
    gchar **parts = g_strsplit(xdg, ":", -1);
    for (gchar **p = parts; *p && **p; p++) {
      g_ptr_array_add(roots, g_build_filename(*p, "icons", NULL));
    }
    g_strfreev(parts);
  } else {
    g_ptr_array_add(roots, g_strdup("/usr/local/share/icons"));
  }
  g_ptr_array_add(roots, g_strdup("/usr/share/icons"));
}

static void br_icon_theme_add_unique(GPtrArray *themes, const char *t) {
  if (!t || !t[0]) {
    return;
  }
  for (guint i = 0; i < themes->len; i++) {
    if (strcmp((char *)g_ptr_array_index(themes, i), t) == 0) {
      return;
    }
  }
  g_ptr_array_add(themes, g_strdup(t));
}

static gboolean br_try_load_icon_file(const char *path, int size_px, GdkPixbuf **out) {
  if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
    return FALSE;
  }
  GError *err = NULL;
  GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(path, size_px, size_px, &err);
  g_clear_error(&err);
  if (!pb) {
    return FALSE;
  }
  *out = pb;
  return TRUE;
}

GdkPixbuf *br_icon_from_theme(const char *name, int size_px) {
  if (!name) {
    return br_stub_icon(NULL, size_px);
  }
  static const int sizes[] = {256, 128, 96, 64, 48, 32, 24, 22, 16, 0};
  static const char *cats[] = {"apps", "mimetypes", NULL};
  static const char *exts[] = {".png", ".svg", ".xpm", NULL};
  g_autofree gchar *down = g_ascii_strdown(name, -1);
  const char *name_variants[4];
  name_variants[0] = name;
  int nv = 1;
  if (down && strcmp(down, name) != 0) {
    name_variants[nv++] = down;
  }
  name_variants[nv] = NULL;

  GPtrArray *roots = g_ptr_array_new_with_free_func(g_free);
  br_icon_roots_collect(roots);

  GPtrArray *themes = g_ptr_array_new_with_free_func(g_free);
  br_icon_theme_add_unique(themes, "hicolor");
  g_autofree gchar *cfg_theme = br_read_user_icon_theme_name();
  if (cfg_theme) {
    br_icon_theme_add_unique(themes, cfg_theme);
  }
  br_icon_theme_add_unique(themes, "Adwaita");
  br_icon_theme_add_unique(themes, "gnome");

  static const char *pixmap_dirs[] = {"/usr/share/pixmaps", "/usr/local/share/pixmaps", NULL};
  for (int pi = 0; pixmap_dirs[pi]; pi++) {
    for (int vi = 0; name_variants[vi]; vi++) {
      for (int ei = 0; exts[ei]; ei++) {
        g_autofree gchar *fn = g_strconcat(name_variants[vi], exts[ei], NULL);
        g_autofree gchar *path = g_build_filename(pixmap_dirs[pi], fn, NULL);
        GdkPixbuf *pb = NULL;
        if (br_try_load_icon_file(path, size_px, &pb)) {
          g_ptr_array_unref(roots);
          g_ptr_array_unref(themes);
          return pb;
        }
      }
    }
  }

  g_autofree gchar *home_pix = g_build_filename(g_get_home_dir(), ".local/share/pixmaps", NULL);
  for (int vi = 0; name_variants[vi]; vi++) {
    for (int ei = 0; exts[ei]; ei++) {
      g_autofree gchar *fn = g_strconcat(name_variants[vi], exts[ei], NULL);
      g_autofree gchar *path = g_build_filename(home_pix, fn, NULL);
      GdkPixbuf *pb = NULL;
      if (br_try_load_icon_file(path, size_px, &pb)) {
        g_ptr_array_unref(roots);
        g_ptr_array_unref(themes);
        return pb;
      }
    }
  }

  for (guint ri = 0; ri < roots->len; ri++) {
    const char *root = (const char *)g_ptr_array_index(roots, ri);
    for (guint ti = 0; ti < themes->len; ti++) {
      const char *theme = (const char *)g_ptr_array_index(themes, ti);
      for (int si = 0; sizes[si]; si++) {
        g_autofree gchar *dim = g_strdup_printf("%dx%d", sizes[si], sizes[si]);
        for (int ci = 0; cats[ci]; ci++) {
          for (int vi = 0; name_variants[vi]; vi++) {
            for (int ei = 0; exts[ei]; ei++) {
              g_autofree gchar *fn = g_strconcat(name_variants[vi], exts[ei], NULL);
              g_autofree gchar *path = g_build_filename(root, theme, dim, cats[ci], fn, NULL);
              GdkPixbuf *pb = NULL;
              if (br_try_load_icon_file(path, size_px, &pb)) {
                g_ptr_array_unref(roots);
                g_ptr_array_unref(themes);
                return pb;
              }
            }
          }
        }
      }
      for (int ci = 0; cats[ci]; ci++) {
        for (int vi = 0; name_variants[vi]; vi++) {
          for (int ei = 0; exts[ei]; ei++) {
            g_autofree gchar *fn = g_strconcat(name_variants[vi], exts[ei], NULL);
            g_autofree gchar *path = g_build_filename(root, theme, "scalable", cats[ci], fn, NULL);
            GdkPixbuf *pb = NULL;
            if (br_try_load_icon_file(path, size_px, &pb)) {
              g_ptr_array_unref(roots);
              g_ptr_array_unref(themes);
              return pb;
            }
          }
        }
      }
    }
  }

  g_ptr_array_unref(roots);
  g_ptr_array_unref(themes);
  return br_stub_icon(name, size_px);
}

static gint app_sort_cb(gconstpointer a, gconstpointer b) {
  const AppEntry *ea = *(const AppEntry **)a;
  const AppEntry *eb = *(const AppEntry **)b;
  const char *na = g_app_info_get_name(G_APP_INFO(ea->info));
  const char *nb = g_app_info_get_name(G_APP_INFO(eb->info));
  return g_utf8_collate(na, nb);
}

GdkPixbuf *app_entry_icon(AppEntry *e) {
  if (!e || !e->info) {
    return NULL;
  }
  if (!e->icon) {
    e->icon = app_load_icon(e->info, 48);
  }
  return e->icon;
}

GdkPixbuf *app_load_icon(GDesktopAppInfo *info, int size_px) {
  GIcon *gicon = g_app_info_get_icon(G_APP_INFO(info));
  if (!gicon) {
    return NULL;
  }
  GError *err = NULL;
  GdkPixbuf *pb = NULL;
  if (G_IS_THEMED_ICON(gicon)) {
    const gchar *const *names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
    if (names && names[0]) {
      pb = br_icon_from_theme(names[0], size_px);
    }
  } else if (G_IS_LOADABLE_ICON(gicon)) {
    GInputStream *st = g_loadable_icon_load(G_LOADABLE_ICON(gicon), size_px, NULL, NULL, &err);
    if (st) {
      pb = gdk_pixbuf_new_from_stream_at_scale(st, size_px, size_px, TRUE, NULL, &err);
      g_object_unref(st);
    }
  }
  if (err) {
    g_error_free(err);
  }
  return pb;
}

GPtrArray *apps_load(void) {
  GList *all = g_app_info_get_all();
  GPtrArray *out = g_ptr_array_new();
  for (GList *l = all; l; l = l->next) {
    GAppInfo *ai = l->data;
    if (!G_IS_DESKTOP_APP_INFO(ai)) {
      continue;
    }
    if (!g_app_info_should_show(ai)) {
      continue;
    }
    GDesktopAppInfo *di = G_DESKTOP_APP_INFO(ai);
    AppEntry *e = g_new0(AppEntry, 1);
    e->info = g_object_ref(di);
    g_ptr_array_add(out, e);
  }
  g_list_free_full(all, g_object_unref);
  g_ptr_array_sort(out, app_sort_cb);
  return out;
}

void apps_free(GPtrArray *apps) {
  if (!apps) {
    return;
  }
  for (guint i = 0; i < apps->len; i++) {
    AppEntry *e = g_ptr_array_index(apps, i);
    g_object_unref(e->info);
    if (e->icon) {
      g_object_unref(e->icon);
    }
    g_free(e);
  }
  g_ptr_array_unref(apps);
}

void apps_filter_indices(const GPtrArray *apps, const char *query, const BrConfig *cfg, gint **out_indices, int *out_n) {
  GArray *a = g_array_new(FALSE, FALSE, sizeof(gint));
  char *qfold = NULL;
  if (query && query[0]) {
    qfold = g_utf8_casefold(query, -1);
  }
  for (guint i = 0; i < apps->len; i++) {
    AppEntry *e = g_ptr_array_index(apps, i);
    const char *id = g_app_info_get_id(G_APP_INFO(e->info));
    if (cfg && cfg->ignored_apps && id && g_hash_table_contains(cfg->ignored_apps, id)) {
      continue;
    }
    const char *name = g_app_info_get_display_name(G_APP_INFO(e->info));
    if (!qfold) {
      gint idx = (gint)i;
      g_array_append_val(a, idx);
      continue;
    }
    char *nfold = g_utf8_casefold(name, -1);
    if (strstr(nfold, qfold)) {
      gint idx = (gint)i;
      g_array_append_val(a, idx);
    }
    g_free(nfold);
  }
  g_free(qfold);
  *out_n = (int)a->len;
  *out_indices = (gint *)g_array_free(a, FALSE);
}
