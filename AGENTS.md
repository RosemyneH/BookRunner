# AGENTS.md

Guidance for AI agents working in this repository.

## Cursor Cloud specific instructions

### Product

Single C11 binary: **BookRunner** — a Wayland launcher using **wlr-layer-shell** (`zwlr_layer_shell_v1`). There is no Node/npm stack and no separate backend service.

### One-time system packages (not in the VM update script)

Match [.github/workflows/ci.yml](.github/workflows/ci.yml) for build deps:

```bash
sudo apt-get install -y --no-install-recommends \
  build-essential meson ninja-build pkg-config \
  libwayland-dev wayland-protocols \
  libcairo2-dev libpango1.0-dev \
  libgdk-pixbuf-2.0-dev libglib2.0-dev libsqlite3-dev libxkbcommon-dev
```

To **run the GUI** in this cloud VM (no real DRM session), also install **Weston** (X11 nested host) and **Sway** (wlroots compositor with layer-shell):

```bash
sudo apt-get install -y --no-install-recommends weston sway wtype scrot
```

### Build / test (same as CI)

From repo root:

```bash
meson setup build --prefix=/usr -Dwerror=false
meson compile -C build
DESTDIR="$(pwd)/staging" meson install -C build
test -x staging/usr/bin/bookrunner
test -f staging/usr/share/applications/bookrunner.desktop
```

There is no dedicated linter or unit-test target; **compile + install layout** is the automated check.

### Running BookRunner locally

Preferred dev entrypoint: `./scripts/run-bookrunner.sh` (see `scripts/run-bookrunner.sh --help`). It expects `build/bookrunner` after Meson compile and sets `GDK_BACKEND=broadway` by default to avoid a second Gdk Wayland client stealing the cursor on Hyprland.

**Requirements:** `XDG_RUNTIME_DIR` must be set and writable; a Wayland compositor that exposes **`zwlr_layer_shell_v1`** (Weston alone does **not** — exit code 3 from `bookrunner_wayland_run`).

### Nested compositor stack (cloud / headless desktop)

When `DISPLAY` is set (e.g. `:1`) but there is no host Wayland session:

1. `export XDG_RUNTIME_DIR=/tmp/bookrunner-runtime-$(id -u)` and `mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"`.
2. In tmux, start Weston on X11: `weston --backend=x11-backend.so --width=1280 --height=800` → socket `wayland-1`.
3. In another tmux session, nested Sway: `WAYLAND_DISPLAY=wayland-1 sway -c /path/to/minimal-config` → socket `wayland-2`.
4. Run BookRunner: `WAYLAND_DISPLAY=wayland-2 ./scripts/run-bookrunner.sh`.

Sway may log `drmGetDevices2 failed` in nested mode; the launcher can still run. Binding `wl_seat` at version 9 against Sway 1.9 (seat v8) can produce a registry warning; if the panel does not appear, use a compositor with seat v9+ (e.g. Hyprland) or adjust seat binding in `src/wayland.c` (out of scope for env-only PRs).

### Maintainer tooling

Regenerate bangs INI: `python3 tools/gen_bangs_ini.py data/bangs_generated.ini` (requires Python 3 only).

### Services summary

| Service | Required for GUI E2E |
|---------|----------------------|
| Weston (X11 backend) | Host display in cloud VM |
| Sway (nested on Weston) | Provides `zwlr_layer_shell_v1` |
| `bookrunner` binary | The app under test |

Optional: `fd`/`plocate` for file search, compositor blur protocols — see [README.md](README.md).
