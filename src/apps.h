#pragma once

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct AppEntry AppEntry;
struct AppEntry {
  GDesktopAppInfo *info;
  GdkPixbuf *icon;
};

GPtrArray *apps_load(void);
void apps_free(GPtrArray *apps);
void apps_filter_indices(const GPtrArray *apps, const char *query, gint **out_indices, int *out_n);
GdkPixbuf *app_load_icon(GDesktopAppInfo *info, int size_px);
GdkPixbuf *br_stub_icon(const char *hint, int size_px);
GdkPixbuf *br_icon_from_theme(const char *name, int size_px);
