#include "usage_db.h"

#include <gio/gio.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct BrUsageDb {
  sqlite3 *db;
  GHashTable *counts;
};

static void free_count(gpointer v) {
  g_free(v);
}

static void br_usage_reload_hash(BrUsageDb *u) {
  g_hash_table_remove_all(u->counts);
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(
          u->db,
          "SELECT desktop_id, count FROM app_launch;",
          -1,
          &st,
          NULL) != SQLITE_OK) {
    return;
  }
  while (sqlite3_step(st) == SQLITE_ROW) {
    const char *id = (const char *)sqlite3_column_text(st, 0);
    gint64 n = sqlite3_column_int64(st, 1);
    if (!id) {
      continue;
    }
    gint64 *slot = g_new(gint64, 1);
    *slot = n;
    g_hash_table_insert(u->counts, g_strdup(id), slot);
  }
  sqlite3_finalize(st);
}

BrUsageDb *br_usage_open(void) {
  g_autofree gchar *dir = g_build_filename(g_get_user_data_dir(), "bookrunner", NULL);
  if (!g_mkdir_with_parents(dir, 0755)) {
    return NULL;
  }
  g_autofree gchar *path = g_build_filename(dir, "usage.sqlite", NULL);
  sqlite3 *db = NULL;
  if (sqlite3_open(path, &db) != SQLITE_OK) {
    if (db) {
      sqlite3_close(db);
    }
    return NULL;
  }
  char *errmsg = NULL;
  if (sqlite3_exec(
          db,
          "PRAGMA journal_mode=WAL;"
          "CREATE TABLE IF NOT EXISTS app_launch ("
          "  desktop_id TEXT PRIMARY KEY NOT NULL,"
          "  count INTEGER NOT NULL,"
          "  last_launch INTEGER NOT NULL"
          ");",
          NULL,
          NULL,
          &errmsg) != SQLITE_OK) {
    g_printerr("bookrunner: usage db init: %s\n", errmsg ? errmsg : "unknown");
    sqlite3_free(errmsg);
    sqlite3_close(db);
    return NULL;
  }
  BrUsageDb *u = g_new0(BrUsageDb, 1);
  u->db = db;
  u->counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_count);
  br_usage_reload_hash(u);
  return u;
}

void br_usage_close(BrUsageDb *u) {
  if (!u) {
    return;
  }
  if (u->db) {
    sqlite3_close(u->db);
  }
  if (u->counts) {
    g_hash_table_unref(u->counts);
  }
  g_free(u);
}

gint64 br_usage_get(const BrUsageDb *u, const char *desktop_id) {
  if (!u || !desktop_id || !desktop_id[0]) {
    return 0;
  }
  gpointer v = g_hash_table_lookup(u->counts, desktop_id);
  if (!v) {
    return 0;
  }
  return *(const gint64 *)v;
}

void br_usage_record(BrUsageDb *u, const char *desktop_id) {
  if (!u || !desktop_id || !desktop_id[0]) {
    return;
  }
  gint64 now = (gint64)time(NULL);
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(
          u->db,
          "INSERT INTO app_launch (desktop_id, count, last_launch) VALUES (?1, 1, ?2) "
          "ON CONFLICT(desktop_id) DO UPDATE SET "
          "count = app_launch.count + 1, last_launch = excluded.last_launch;",
          -1,
          &st,
          NULL) != SQLITE_OK) {
    return;
  }
  sqlite3_bind_text(st, 1, desktop_id, -1, SQLITE_STATIC);
  sqlite3_bind_int64(st, 2, now);
  sqlite3_step(st);
  sqlite3_finalize(st);

  if (sqlite3_prepare_v2(
          u->db,
          "SELECT count FROM app_launch WHERE desktop_id = ?1;",
          -1,
          &st,
          NULL) != SQLITE_OK) {
    return;
  }
  sqlite3_bind_text(st, 1, desktop_id, -1, SQLITE_STATIC);
  gint64 n = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
    n = sqlite3_column_int64(st, 0);
  }
  sqlite3_finalize(st);

  gpointer old = g_hash_table_lookup(u->counts, desktop_id);
  if (old) {
    *(gint64 *)old = n;
  } else {
    gint64 *slot = g_new(gint64, 1);
    *slot = n;
    g_hash_table_insert(u->counts, g_strdup(desktop_id), slot);
  }
}
