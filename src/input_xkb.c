#include "input_xkb.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>

void input_xkb_init(InputXkb *ix) {
  memset(ix, 0, sizeof(*ix));
  ix->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

void input_xkb_fini(InputXkb *ix) {
  if (ix->state) {
    xkb_state_unref(ix->state);
  }
  if (ix->keymap) {
    xkb_keymap_unref(ix->keymap);
  }
  if (ix->ctx) {
    xkb_context_unref(ix->ctx);
  }
  memset(ix, 0, sizeof(*ix));
}

void input_xkb_set_keymap(InputXkb *ix, int fd, size_t size) {
  if (ix->state) {
    xkb_state_unref(ix->state);
    ix->state = NULL;
  }
  if (ix->keymap) {
    xkb_keymap_unref(ix->keymap);
    ix->keymap = NULL;
  }
  char *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map_str == MAP_FAILED) {
    return;
  }
  ix->keymap = xkb_keymap_new_from_string(ix->ctx, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  close(fd);
  if (ix->keymap) {
    ix->state = xkb_state_new(ix->keymap);
  }
}

void input_xkb_key(InputXkb *ix, uint32_t key, uint32_t wl_state) {
  if (!ix->state) {
    return;
  }
  enum xkb_key_direction dir = wl_state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP;
  xkb_state_update_key(ix->state, key + 8, dir);
}

bool input_xkb_mod_ctrl(const InputXkb *ix) {
  if (!ix->state) {
    return false;
  }
  return xkb_state_mod_name_is_active(ix->state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
}

xkb_keysym_t input_xkb_key_sym(const InputXkb *ix, uint32_t key) {
  if (!ix->state) {
    return XKB_KEY_NoSymbol;
  }
  return xkb_state_key_get_one_sym(ix->state, key + 8);
}
