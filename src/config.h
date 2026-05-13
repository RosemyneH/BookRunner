#pragma once

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  BR_FILE_BACKEND_FD,
  BR_FILE_BACKEND_PLOCATE,
  BR_FILE_BACKEND_AUTO,
} BrFileBackend;

typedef struct BrConfig BrConfig;
struct BrConfig {
  int ui_width;
  int ui_height;
  char *font;
  double font_size;
  char *bang_prefix;
  int max_visible_rows;
  int debounce_ms;
  int file_timeout_ms;
  int max_file_results;
  BrFileBackend file_backend;
  char *fd_command;
  char *plocate_command;
  GStrv file_roots;

  uint32_t col_panel;
  uint32_t col_input_bg;
  uint32_t col_text;
  uint32_t col_dim;
  uint32_t col_border;
  uint32_t col_row_sel;

  bool list_wrap;
  bool invert_list_wheel;

  GHashTable *bangs;
};

void br_config_init_defaults(BrConfig *c);
void br_config_load(BrConfig *c);
void br_config_clear(BrConfig *c);
