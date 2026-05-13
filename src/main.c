#include "context.h"
#include "single_instance.h"

#include "br_state.h"

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

static GdkPixbuf *load_icon(const char *icon_name) {
  return br_icon_from_theme(icon_name, 32);
}

int main(void) {
  if (!g_getenv("GDK_BACKEND")) {
    g_setenv("GDK_BACKEND", "broadway", TRUE);
  }
  int instance_listen_fd = -1;
  if (!br_single_instance_acquire(&instance_listen_fd)) {
    return 0;
  }
  AppContext ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.instance_listen_fd = instance_listen_fd;
  br_arena_init(&ctx.candidate_arena);
  br_config_load(&ctx.config);
  br_state_load(&ctx.config);
  ctx.usage_db = br_usage_open();
  ctx.apps = apps_load();
  br_file_search_init(&ctx.file_search);
  ctx.icon_file = load_icon("text-x-generic");
  br_ctx_refilter(&ctx);
  br_file_search_on_query_changed(&ctx);

  int rc = bookrunner_wayland_run(&ctx);

  if (ctx.launch_uri) {
    GError *err = NULL;
    if (!g_app_info_launch_default_for_uri(ctx.launch_uri, NULL, &err)) {
      g_printerr("bookrunner: %s\n", err ? err->message : "launch uri failed");
      g_clear_error(&err);
    }
  } else if (ctx.launch_file) {
    GError *err = NULL;
    g_autofree gchar *uri = g_filename_to_uri(ctx.launch_file, NULL, &err);
    if (uri && !g_app_info_launch_default_for_uri(uri, NULL, &err)) {
      g_printerr("bookrunner: %s\n", err ? err->message : "launch file failed");
      g_clear_error(&err);
    }
  } else if (ctx.launch_app) {
    GError *err = NULL;
    if (!g_app_info_launch(G_APP_INFO(ctx.launch_app), NULL, NULL, &err)) {
      g_printerr("bookrunner: %s\n", err ? err->message : "launch app failed");
      g_clear_error(&err);
    } else {
      br_usage_record(ctx.usage_db, g_app_info_get_id(G_APP_INFO(ctx.launch_app)));
    }
  }

  br_ctx_free_launch_fields(&ctx);
  if (ctx.candidates) {
    br_ctx_candidates_clear(&ctx);
    g_ptr_array_unref(ctx.candidates);
    ctx.candidates = NULL;
  }
  apps_free(ctx.apps);
  br_file_search_fini(&ctx.file_search);
  br_usage_close(ctx.usage_db);
  ctx.usage_db = NULL;
  br_arena_free(&ctx.candidate_arena);
  br_config_clear(&ctx.config);
  g_clear_object(&ctx.icon_file);
  br_single_instance_release(instance_listen_fd);
  return rc == 0 ? 0 : (rc > 0 ? rc : 1);
}
