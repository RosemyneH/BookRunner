#pragma once

#include <stddef.h>

typedef struct BrBlock BrBlock;
struct BrBlock {
  BrBlock *next;
  size_t used;
  size_t cap;
};

typedef struct BrArena {
  BrBlock *first;
  BrBlock *tail;
} BrArena;

void br_arena_init(BrArena *a);
void br_arena_reset(BrArena *a);
void br_arena_free(BrArena *a);
void *br_arena_alloc(BrArena *a, size_t size, size_t align);
char *br_arena_strdup(BrArena *a, const char *s);
