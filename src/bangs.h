#pragma once

#include "config.h"

#include <stdbool.h>

bool br_bang_parse(const char *query, const BrConfig *cfg, char **out_keyword, char **out_tail);
char *br_bang_build_url(const char *template_, const char *tail);
