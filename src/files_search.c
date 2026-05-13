#include "files_search.h"

#include "bangs.h"
#include "context.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  AppContext *ctx;
  uint64_t gen;
  char query[BR_MAX_QUERY];
} BrFileJob;

static int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

gboolean br_file_search_effective_query(const AppContext *ctx, char *buf, size_t buflen) {
  if (br_ctx_bang_followup_active(ctx)) {
    if (strcmp(ctx->bang_follow_kw, "fi") == 0 ||
        (ctx->config.bang_f_enabled && strcmp(ctx->bang_follow_kw, "f") == 0)) {
      g_strlcpy(buf, ctx->bang_follow_q, buflen);
      return TRUE;
    }
    return FALSE;
  }
  g_autofree gchar *kw = NULL;
  g_autofree gchar *tail = NULL;
  if (!br_bang_parse(ctx->query, &ctx->config, &kw, &tail)) {
    g_strlcpy(buf, ctx->query, buflen);
    return TRUE;
  }
  if (g_strcmp0(kw, "set") == 0) {
    return FALSE;
  }
  if (g_strcmp0(kw, "fi") == 0) {
    g_strlcpy(buf, tail ? tail : "", buflen);
    return TRUE;
  }
  if (ctx->config.bang_f_enabled && g_strcmp0(kw, "f") == 0) {
    g_strlcpy(buf, tail ? tail : "", buflen);
    return TRUE;
  }
  return FALSE;
}

static GStrv run_fd_subprocess(const AppContext *ctx, const char *q, GError **err) {
  const BrConfig *cfg = &ctx->config;
  if (!q || !*q) {
    return NULL;
  }
  g_autofree gchar *bin = g_find_program_in_path(cfg->fd_command);
  if (!bin) {
    bin = g_find_program_in_path("fdfind");
  }
  if (!bin) {
    return NULL;
  }
  g_autofree gchar *nstr = g_strdup_printf("%d", cfg->max_file_results);
  GPtrArray *argv = g_ptr_array_new();
  g_ptr_array_add(argv, bin);
  g_ptr_array_add(argv, (gpointer) "--max-results");
  g_ptr_array_add(argv, (gpointer) nstr);
  g_ptr_array_add(argv, (gpointer) "--full-path");
  g_ptr_array_add(argv, (gpointer) q);
  for (char **r = cfg->file_roots; r && *r; r++) {
    g_ptr_array_add(argv, *r);
  }
  g_ptr_array_add(argv, NULL);
  GSubprocess *sp = g_subprocess_newv((const gchar *const *)argv->pdata, G_SUBPROCESS_FLAGS_STDOUT_PIPE, err);
  g_ptr_array_unref(argv);
  if (!sp) {
    return NULL;
  }
  gchar *out = NULL;
  gchar *errout = NULL;
  gboolean ok = g_subprocess_communicate_utf8(sp, NULL, NULL, &out, &errout, err);
  g_object_unref(sp);
  if (!ok) {
    g_free(out);
    g_free(errout);
    return NULL;
  }
  g_free(errout);
  if (!out || !*out) {
    g_free(out);
    return NULL;
  }
  gchar **lines = g_strsplit(out, "\n", 0);
  g_free(out);
  return lines;
}

static GStrv run_plocate_subprocess(const AppContext *ctx, const char *q, GError **err) {
  const BrConfig *cfg = &ctx->config;
  if (!q || !*q) {
    return NULL;
  }
  g_autofree gchar *bin = g_find_program_in_path(cfg->plocate_command);
  if (!bin) {
    bin = g_find_program_in_path("locate");
  }
  if (!bin) {
    return NULL;
  }
  GPtrArray *argv = g_ptr_array_new();
  g_ptr_array_add(argv, bin);
  g_ptr_array_add(argv, (gpointer) q);
  g_ptr_array_add(argv, NULL);
  GSubprocess *sp = g_subprocess_newv((const gchar *const *)argv->pdata, G_SUBPROCESS_FLAGS_STDOUT_PIPE, err);
  g_ptr_array_unref(argv);
  if (!sp) {
    return NULL;
  }
  gchar *out = NULL;
  gchar *errout = NULL;
  gboolean ok = g_subprocess_communicate_utf8(sp, NULL, NULL, &out, &errout, err);
  g_object_unref(sp);
  if (!ok) {
    g_free(out);
    g_free(errout);
    return NULL;
  }
  g_free(errout);
  if (!out || !*out) {
    g_free(out);
    return NULL;
  }
  gchar **lines = g_strsplit(out, "\n", 0);
  g_free(out);
  return lines;
}

static void *br_file_worker(void *arg) {
  BrFileJob *job = arg;
  AppContext *ctx = job->ctx;
  BrFileSearch *fs = &ctx->file_search;
  GError *err = NULL;
  GStrv lines = NULL;
  if (ctx->config.file_backend == BR_FILE_BACKEND_PLOCATE) {
    lines = run_plocate_subprocess(ctx, job->query, &err);
  } else {
    lines = run_fd_subprocess(ctx, job->query, &err);
  }
  if (err) {
    g_error_free(err);
  }
  pthread_mutex_lock(&fs->mx);
  if (job->gen != fs->gen) {
    pthread_mutex_unlock(&fs->mx);
    g_strfreev(lines);
    g_free(job);
    char b = 1;
    (void)write(fs->notify_pipe[1], &b, 1);
    return NULL;
  }
  g_ptr_array_remove_range(fs->paths, 0, fs->paths->len);
  if (lines) {
    int cap = ctx->config.max_file_results;
    for (int i = 0; lines[i] && i < cap; i++) {
      if (!lines[i][0]) {
        continue;
      }
      if (lines[i][0] != '/') {
        continue;
      }
      g_ptr_array_add(fs->paths, g_strdup(lines[i]));
    }
  }
  g_strfreev(lines);
  pthread_mutex_unlock(&fs->mx);
  g_free(job);
  char b = 1;
  (void)write(fs->notify_pipe[1], &b, 1);
  return NULL;
}

void br_file_search_init(BrFileSearch *fs) {
  memset(fs, 0, sizeof(*fs));
  pthread_mutex_init(&fs->mx, NULL);
  fs->paths = g_ptr_array_new_with_free_func(g_free);
  if (pipe(fs->notify_pipe) != 0) {
    fs->notify_pipe[0] = -1;
    fs->notify_pipe[1] = -1;
  }
}

void br_file_search_fini(BrFileSearch *fs) {
  if (fs->notify_pipe[0] >= 0) {
    close(fs->notify_pipe[0]);
  }
  if (fs->notify_pipe[1] >= 0) {
    close(fs->notify_pipe[1]);
  }
  pthread_mutex_destroy(&fs->mx);
  if (fs->paths) {
    g_ptr_array_unref(fs->paths);
  }
  memset(fs, 0, sizeof(*fs));
}

void br_file_search_on_query_changed(AppContext *ctx) {
  BrFileSearch *fs = &ctx->file_search;
  char eff[BR_MAX_QUERY];
  if (!br_file_search_effective_query(ctx, eff, sizeof eff)) {
    pthread_mutex_lock(&fs->mx);
    fs->want_search = false;
    g_ptr_array_remove_range(fs->paths, 0, fs->paths->len);
    pthread_mutex_unlock(&fs->mx);
    return;
  }
  pthread_mutex_lock(&fs->mx);
  fs->want_search = true;
  fs->deadline_ms = now_ms() + ctx->config.debounce_ms;
  g_ptr_array_remove_range(fs->paths, 0, fs->paths->len);
  pthread_mutex_unlock(&fs->mx);
}

void br_file_search_poll(AppContext *ctx) {
  BrFileSearch *fs = &ctx->file_search;
  char eff[BR_MAX_QUERY];
  if (!br_file_search_effective_query(ctx, eff, sizeof eff)) {
    return;
  }
  pthread_mutex_lock(&fs->mx);
  if (!fs->want_search) {
    pthread_mutex_unlock(&fs->mx);
    return;
  }
  if (fs->worker_live) {
    pthread_mutex_unlock(&fs->mx);
    return;
  }
  if (now_ms() < fs->deadline_ms) {
    pthread_mutex_unlock(&fs->mx);
    return;
  }
  fs->worker_live = true;
  fs->want_search = false;
  fs->gen++;
  uint64_t g = fs->gen;
  pthread_mutex_unlock(&fs->mx);

  BrFileJob *job = g_new0(BrFileJob, 1);
  job->ctx = ctx;
  job->gen = g;
  g_strlcpy(job->query, eff, sizeof(job->query));

  pthread_t tid;
  if (pthread_create(&tid, NULL, br_file_worker, job) != 0) {
    pthread_mutex_lock(&fs->mx);
    fs->worker_live = false;
    pthread_mutex_unlock(&fs->mx);
    g_free(job);
    return;
  }
  pthread_detach(tid);
}

void br_file_search_drain_notify(AppContext *ctx) {
  BrFileSearch *fs = &ctx->file_search;
  if (fs->notify_pipe[0] < 0) {
    return;
  }
  char buf[32];
  (void)read(fs->notify_pipe[0], buf, sizeof buf);
  pthread_mutex_lock(&fs->mx);
  fs->worker_live = false;
  pthread_mutex_unlock(&fs->mx);
}
