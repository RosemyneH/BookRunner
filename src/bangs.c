#include "bangs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *s) {
  while (*s && isspace((unsigned char)*s)) {
    s++;
  }
  return s;
}

bool br_bang_parse(const char *query, const BrConfig *cfg, char **out_keyword, char **out_tail) {
  *out_keyword = NULL;
  *out_tail = NULL;
  if (!query || !cfg->bang_prefix || !cfg->bang_prefix[0]) {
    return false;
  }
  size_t plen = strlen(cfg->bang_prefix);
  if (strncmp(query, cfg->bang_prefix, plen) != 0) {
    return false;
  }
  const char *rest = skip_ws(query + plen);
  if (!*rest) {
    *out_keyword = g_strdup("");
    *out_tail = g_strdup("");
    return true;
  }
  const char *sp = rest;
  while (*sp && !isspace((unsigned char)*sp)) {
    sp++;
  }
  size_t kwlen = (size_t)(sp - rest);
  *out_keyword = g_strndup(rest, kwlen);
  const char *tail = skip_ws(sp);
  *out_tail = g_strdup(tail);
  return true;
}

char *br_bang_build_url(const char *template_, const char *tail) {
  if (!template_) {
    return NULL;
  }
  g_autofree gchar *esc = g_uri_escape_string(tail ? tail : "", NULL, FALSE);
  if (strstr(template_, "%s")) {
    return g_strdup_printf(template_, esc);
  }
  if (tail && *tail) {
    gchar *join = strchr(template_, '?') ? g_strdup_printf("%s&%s", template_, esc) : g_strdup_printf("%s?%s", template_, esc);
    return join;
  }
  return g_strdup(template_);
}
