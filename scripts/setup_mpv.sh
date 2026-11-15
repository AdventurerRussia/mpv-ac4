#!/usr/bin/env bash
set -e

cd /r
rm -rf /r/mpv-build

export PKG_CONFIG_PATH="/r/install/lib/pkgconfig:$PKG_CONFIG_PATH"
export PATH="/r/install/bin:/ucrt64/bin:$PATH"

echo "=== pkg-config verification ==="
pkg-config --modversion libavcodec
pkg-config --variable=prefix libavcodec
pkg-config --modversion libswresample
pkg-config --variable=prefix libswresample

echo "=== Running meson setup for mpv ==="
meson setup R:/mpv-build R:/src/mpv \
    --prefix=R:/mpv-build/install_prefix \
    --pkg-config-path=R:/install/lib/pkgconfig \
    -Dtests=false \
    -Dmanpage-build=disabled
