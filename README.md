# BookRunner

[![CI](https://github.com/RosemyneH/BookRunner/actions/workflows/ci.yml/badge.svg)](https://github.com/RosemyneH/BookRunner/actions/workflows/ci.yml)

BookRunner is a **Wayland** application launcher built on **wlr-layer-shell**. It lists installed applications, integrates optional file search, supports configurable **bang** URLs, and is meant to stay small and fast.

**License:** [MIT](LICENSE)

## Requirements

- A Wayland compositor with **wlr-layer-shell** (e.g. Hyprland, Sway, many wlroots-based sessions).
- GTK-related libraries are pulled in transitively (e.g. GdkPixbuf for icons); the UI surface itself is Cairo + Pango.

## Install

### From GitHub Releases

On each **annotated or lightweight tag** matching `v*`, the [Release workflow](.github/workflows/release.yml) publishes:

| Artifact | Notes |
|----------|--------|
| `bookrunner_<version>_amd64.deb` | Debian/Ubuntu-style package. |
| `bookrunner-<version>-1.x86_64.rpm` | RPM for Fedora/RHEL-style distros (depends names target Fedora). |
| `bookrunner-<version>-x86_64.AppImage` | Self-contained bundle; uses [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) + GTK plugin for GdkPixbuf loaders. |
| `SHA256SUMS` | Checksums for the above. |

Install examples:

```bash
sudo apt install ./bookrunner_*_amd64.deb
```

```bash
sudo rpm -Uvh bookrunner-*-1.x86_64.rpm
```

```bash
chmod +x bookrunner-*-x86_64.AppImage
./bookrunner-*-x86_64.AppImage
```

If the AppImage does not start on older systems, ensure FUSE or `libfuse2` is available, or run with `APPIMAGE_EXTRACT_AND_RUN=1 ./bookrunner-*-x86_64.AppImage`.

### From source

Dependencies (Debian/Ubuntu names):

`meson`, `ninja-build`, `pkg-config`, `libwayland-dev`, `wayland-protocols`, `libwayland-cursor-dev`, `libcairo2-dev`, `libpango1.0-dev`, `libgdk-pixbuf-2.0-dev`, `libglib2.0-dev`, `libsqlite3-dev`, `libxkbcommon-dev`

```bash
meson setup build --prefix=/usr -Dbuildtype=release
meson compile -C build
sudo meson install -C build
```

This installs `bookrunner`, the [desktop entry](data/bookrunner.desktop), and the [icon](data/icons/hicolor/scalable/apps/bookrunner.svg).

### Run from build tree (developers)

```bash
./scripts/run-bookrunner.sh
```

See `scripts/run-bookrunner.sh --help` for Wayland/GDK-related options.

## Configuration

User config path: `~/.config/bookrunner/config.ini` (see `src/config.c` for supported keys).

## Single instance

A second launch while BookRunner is running connects to a socket under `$XDG_RUNTIME_DIR` and **closes the existing instance** without starting a second window. If `XDG_RUNTIME_DIR` is unset, this behavior is skipped.

## Contributing

Issues and pull requests are welcome. Use the [bug report](.github/ISSUE_TEMPLATE/bug_report.yml) or [feature request](.github/ISSUE_TEMPLATE/feature_request.yml) templates when it helps.

## Releasing (maintainers)

1. Bump `version` in `meson.build` if needed.
2. Create and push a tag: `git tag v0.2.0 && git push origin v0.2.0`
3. The **Release** workflow builds binaries and attaches them to a GitHub Release with auto-generated notes.

## Repository layout

| Path | Role |
|------|------|
| `src/` | Application source |
| `protocols/` | Wayland protocol XML |
| `data/` | `.desktop` and icon for installs / AppImage |
| `packaging/` | `nfpm` packaging definition |
| `.github/workflows/` | CI and release automation |
