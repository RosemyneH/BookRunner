#!/usr/bin/env bash
set -euo pipefail
VERSION="${1:?usage: ci_package_linux.sh <package-version>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

rm -rf build staging dist AppDir bookrunner.png ./*.AppImage ./BookRunner*.AppImage 2>/dev/null || true

meson setup build --prefix=/usr --buildtype=release -Dwerror=false
meson compile -C build
DESTDIR="$ROOT/staging" meson install -C build

mkdir -p dist
if [[ ! -x /tmp/nfpm-bookrunner ]]; then
  curl -sSfL https://github.com/goreleaser/nfpm/releases/download/v2.41.1/nfpm_2.41.1_Linux_x86_64.tar.gz | tar xz -C /tmp nfpm
  install -m755 /tmp/nfpm /tmp/nfpm-bookrunner
fi
sed "s/__VERSION__/${VERSION}/g" packaging/nfpm.yaml > /tmp/nfpm-bookrunner.yaml
/tmp/nfpm-bookrunner package -f /tmp/nfpm-bookrunner.yaml -p deb --target dist/
/tmp/nfpm-bookrunner package -f /tmp/nfpm-bookrunner.yaml -p rpm --target dist/

mkdir -p AppDir
cp -a staging/usr AppDir/
rsvg-convert -w 256 data/icons/hicolor/scalable/apps/bookrunner.svg -o bookrunner.png
curl -sSfLO https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -sSfLO https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-x86_64.AppImage
curl -sSfLo linuxdeploy-plugin-gtk.sh \
  https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/3b67a1d1c1b0c8268f57f2bce40fe2d33d409cea/linuxdeploy-plugin-gtk.sh
chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-appimage-x86_64.AppImage linuxdeploy-plugin-gtk.sh
export ARCH=x86_64
export APPIMAGE_EXTRACT_AND_RUN=1
export DEPLOY_GTK_VERSION=3
./linuxdeploy-x86_64.AppImage --appdir AppDir \
  -e AppDir/usr/bin/bookrunner \
  -d data/bookrunner.desktop \
  -i bookrunner.png \
  --plugin gtk \
  --plugin appimage
shopt -s nullglob
out=""
for f in ./*.AppImage ./BookRunner*.AppImage; do
  if [[ -f "$f" ]]; then
    out="$f"
    break
  fi
done
if [[ -z "${out}" ]]; then
  echo "linuxdeploy did not produce an AppImage" >&2
  ls -la
  exit 1
fi
mv "$out" "dist/bookrunner-${VERSION}-x86_64.AppImage"

(cd dist && sha256sum * > SHA256SUMS)
cat dist/SHA256SUMS
