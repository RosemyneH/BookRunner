# BookRunner

[![CI](https://github.com/RosemyneH/BookRunner/actions/workflows/ci.yml/badge.svg)](https://github.com/RosemyneH/BookRunner/actions/workflows/ci.yml)

BookRunner is a **Wayland** application launcher built on **wlr-layer-shell**. It lists installed applications, integrates optional file search, supports configurable **bang** URLs, and is meant to stay small and fast.

**License:** [MIT](LICENSE)

## Requirements

- A Wayland compositor with **wlr-layer-shell** (e.g. Hyprland, Sway, many wlroots-based sessions).
- GTK-related libraries are pulled in transitively (e.g. GdkPixbuf for icons); the UI surface itself is Cairo + Pango.

## Install

### From GitHub Releases

**Snapshot builds:** every push to `main` / `master` that passes [CI](.github/workflows/ci.yml) creates a **prerelease** tagged like `snapshot-0.1.0-ci.<run_id>` with **.deb**, **.rpm**, **AppImage**, **SHA256SUMS**, and auto-generated notes (same artifacts as stable releases). The repository must allow **Workflow permissions → Read and write** (Settings → Actions → General) so the default `GITHUB_TOKEN` can create tags and releases.

**Stable artifacts:** pushing a **SemVer tag** matching `v*` (for example `v0.2.0`) runs the [Release workflow](.github/workflows/release.yml) and attaches:

| Artifact | Notes |
|----------|--------|
| `bookrunner_<version>-1_amd64.deb` | Debian/Ubuntu-style package (`nfpm` default naming). |
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

`meson`, `ninja-build`, `pkg-config`, `libwayland-dev` (includes Wayland cursor headers), `wayland-protocols`, `libcairo2-dev`, `libpango1.0-dev` (includes Pangocairo), `libgdk-pixbuf-2.0-dev`, `libglib2.0-dev`, `libsqlite3-dev`, `libxkbcommon-dev`

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

### Bangs (web shortcuts)

Built-in **default bang URLs are not hardcoded** anymore. The shipped list lives in **`data/bangs_generated.ini`** (installed to `…/share/bookrunner/bangs_generated.ini`) with three sections:

- **`[bangs]`** — `keyword=https://…%s…` templates (`%s` is the escaped query).
- **`[bang_desc]`** — optional friendly titles for the launcher list.
- **`[bang_icons]`** — freedesktop-style icon names passed to Gtk’s icon theme (missing names fall back to `applications-internet`).

Merge order: packaged ini → optional `BOOKRUNNER_BANGS_INI` → `~/.local/share/bookrunner/bangs_generated.ini` → **`[bangs]` / `[bang_desc]` / `[bang_icons]` in `config.ini`** (later wins on duplicate keys).

If you previously had a huge auto-generated `bangs_generated.ini` under `~/.local/share/bookrunner/`, remove it so the packaged curated list applies (or replace it with your own).

Regenerate the curated list after editing `tools/gen_bangs_ini.py`:

```bash
python3 tools/gen_bangs_ini.py data/bangs_generated.ini
```

## Single instance

A second launch while BookRunner is running connects to a socket under `$XDG_RUNTIME_DIR` and **closes the existing instance** without starting a second window. If `XDG_RUNTIME_DIR` is unset, this behavior is skipped.

## Contributing

Issues and pull requests are welcome. Use the [bug report](.github/ISSUE_TEMPLATE/bug_report.yml) or [feature request](.github/ISSUE_TEMPLATE/feature_request.yml) templates when it helps.

## Releasing (maintainers)

**Continuous snapshots** attach Linux packages on each successful push to `main` / `master` (see CI workflow).

For a **stable** release with the same artifacts:

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
