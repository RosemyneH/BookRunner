#pragma once

#include <glib.h>

typedef struct BrUsageDb BrUsageDb;

BrUsageDb *br_usage_open(void);
void br_usage_close(BrUsageDb *u);
gint64 br_usage_get(const BrUsageDb *u, const char *desktop_id);
void br_usage_record(BrUsageDb *u, const char *desktop_id);
