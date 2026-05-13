#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

typedef struct InputXkb InputXkb;
struct InputXkb {
  struct xkb_context *ctx;
  struct xkb_keymap *keymap;
  struct xkb_state *state;
};

void input_xkb_init(InputXkb *ix);
void input_xkb_fini(InputXkb *ix);
void input_xkb_set_keymap(InputXkb *ix, int fd, size_t size);
void input_xkb_key(InputXkb *ix, uint32_t key, uint32_t wl_state);
bool input_xkb_mod_ctrl(const InputXkb *ix);
xkb_keysym_t input_xkb_key_sym(const InputXkb *ix, uint32_t key);
