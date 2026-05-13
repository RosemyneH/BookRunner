#pragma once

#include "config.h"

#include <gio/gio.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct AppContext AppContext;

typedef struct BrFileSearch BrFileSearch;
struct BrFileSearch {
  pthread_mutex_t mx;
  pthread_t worker;
  bool worker_live;
  int notify_pipe[2];
  uint64_t gen;
  char last_started_query[512];
  GPtrArray *paths;
  bool want_search;
  int64_t deadline_ms;
};

void br_file_search_init(BrFileSearch *fs);
void br_file_search_fini(BrFileSearch *fs);
void br_file_search_on_query_changed(AppContext *ctx);
void br_file_search_poll(AppContext *ctx);
void br_file_search_drain_notify(AppContext *ctx);
