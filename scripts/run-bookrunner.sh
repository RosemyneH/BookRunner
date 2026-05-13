#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
use_gdb=0
verbose=0
br_args=()

while (($#)); do
	case "$1" in
	--help | -h)
		cat <<'EOF'
Usage: run-bookrunner.sh [options] [--] [bookrunner-args...]

Options:
  --trace-wayland   export WAYLAND_DEBUG=client (noisy; for pointer/cursor issues)
  --gdb             run under gdb --args
  --gdk-wayland     force GDK_BACKEND=wayland (reproduce Gdk opening Wayland)
  -v, --verbose     print resolved binary and effective Wayland env to stderr
  -h, --help        this text

If WAYLAND_DISPLAY is unset, tries wayland-1 when that socket exists under
XDG_RUNTIME_DIR, else falls back to wayland-1 (Hyprland default).

If GDK_BACKEND is unset, sets GDK_BACKEND=broadway so Gdk/GTK (pulled in via
GIO/GdkPixbuf) does not open a second Wayland connection; that extra client
often issues wl_pointer.set_cursor(nil) and hides the cursor on Hyprland.
Use --gdk-wayland to opt back into Gdk on Wayland for A/B tests.

Examples:
  scripts/run-bookrunner.sh -v
  WAYLAND_DISPLAY=wayland-1 scripts/run-bookrunner.sh --trace-wayland
EOF
		exit 0
		;;
	--trace-wayland)
		export WAYLAND_DEBUG=client
		shift
		;;
	--gdk-wayland)
		export GDK_BACKEND=wayland
		shift
		;;
	--gdb) use_gdb=1 ; shift ;;
	-v | --verbose) verbose=1 ; shift ;;
	--)
		shift
		br_args+=("$@")
		break
		;;
	-*)
		echo "unknown option: $1 (try --help)" >&2
		exit 2
		;;
	*)
		br_args+=("$@")
		break
		;;
	esac
done

bin=""
for c in "$root/build/bookrunner" "$root/build-manual/bookrunner"; do
	if [[ -x "$c" ]]; then
		bin=$c
		break
	fi
done
if [[ -z "$bin" ]]; then
	echo "run-bookrunner: no executable at build/bookrunner or build-manual/bookrunner" >&2
	echo "  (from $root — run meson setup + meson compile)" >&2
	exit 1
fi

rt="${XDG_RUNTIME_DIR:-}"
if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
	if [[ -n "$rt" && -S "$rt/wayland-1" ]]; then
		export WAYLAND_DISPLAY=wayland-1
	elif [[ -n "${WAYLAND_SOCKET:-}" ]]; then
		export WAYLAND_DISPLAY="$WAYLAND_SOCKET"
	else
		export WAYLAND_DISPLAY=wayland-1
	fi
	[[ "$verbose" -eq 1 ]] && echo "run-bookrunner: set WAYLAND_DISPLAY=$WAYLAND_DISPLAY (was unset)" >&2
fi

if [[ -z "${GDK_BACKEND:-}" ]]; then
	export GDK_BACKEND=broadway
	[[ "$verbose" -eq 1 ]] &&
		echo "run-bookrunner: set GDK_BACKEND=broadway (avoid Gdk second Wayland client / nil cursor)" >&2
fi

if [[ "$verbose" -eq 1 ]]; then
	echo "run-bookrunner: bin=$bin" >&2
	echo "run-bookrunner: WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}" >&2
	echo "run-bookrunner: GDK_BACKEND=$GDK_BACKEND" >&2
	[[ -n "${WAYLAND_DEBUG:-}" ]] && echo "run-bookrunner: WAYLAND_DEBUG=$WAYLAND_DEBUG" >&2
fi

cd "$root"
if [[ "$use_gdb" -eq 1 ]]; then
	exec gdb -q --args "$bin" "${br_args[@]}"
else
	exec "$bin" "${br_args[@]}"
fi
