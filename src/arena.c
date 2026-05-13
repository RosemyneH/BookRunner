#include "arena.h"

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static size_t br_block_hdr(void) {
  return (sizeof(BrBlock) + sizeof(max_align_t) - 1) & ~(sizeof(max_align_t) - 1);
}

static unsigned char *br_block_data(const BrBlock *b) {
  return (unsigned char *)b + br_block_hdr();
}

static void *try_block_alloc(BrBlock *b, size_t size, size_t align) {
  if (align < sizeof(void *)) {
    align = sizeof(void *);
  }
  unsigned char *base = br_block_data(b);
  uintptr_t start = (uintptr_t)base + b->used;
  uintptr_t aligned = (start + align - 1) & ~(uintptr_t)(align - 1);
  size_t off = aligned - (uintptr_t)base;
  if (off + size > b->cap || size == 0) {
    return NULL;
  }
  b->used = off + size;
  return (void *)aligned;
}

void br_arena_init(BrArena *a) {
  a->first = NULL;
  a->tail = NULL;
}

void br_arena_reset(BrArena *a) {
  for (BrBlock *b = a->first; b; b = b->next) {
    b->used = 0;
  }
}

void br_arena_free(BrArena *a) {
  BrBlock *b = a->first;
  while (b) {
    BrBlock *n = b->next;
    g_free(b);
    b = n;
  }
  a->first = NULL;
  a->tail = NULL;
}

void *br_arena_alloc(BrArena *a, size_t size, size_t align) {
  if (size == 0) {
    return NULL;
  }
  for (BrBlock *b = a->first; b; b = b->next) {
    void *p = try_block_alloc(b, size, align);
    if (p) {
      return p;
    }
  }
  size_t bcap = 4096;
  size_t align_slack = align < sizeof(void *) ? sizeof(void *) : align;
  size_t min_data = align_slack + size;
  while (bcap < min_data) {
    bcap *= 2;
  }
  BrBlock *nb = g_malloc(br_block_hdr() + bcap);
  nb->next = NULL;
  nb->used = 0;
  nb->cap = bcap;
  if (!a->first) {
    a->first = nb;
  } else {
    a->tail->next = nb;
  }
  a->tail = nb;
  return try_block_alloc(nb, size, align);
}

char *br_arena_strdup(BrArena *a, const char *s) {
  if (!s) {
    return NULL;
  }
  size_t n = strlen(s) + 1;
  char *d = br_arena_alloc(a, n, _Alignof(char));
  if (!d) {
    return NULL;
  }
  memcpy(d, s, n);
  return d;
}
