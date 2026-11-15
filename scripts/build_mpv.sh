#!/usr/bin/env bash
set -e

export PKG_CONFIG_PATH="/r/install/lib/pkgconfig:$PKG_CONFIG_PATH"
export PATH="/r/install/bin:/ucrt64/bin:$PATH"

echo "=== Compiling mpv with ninja ==="
ninja -C /r/mpv-build

echo "=== Installing mpv to temporary build prefix ==="
ninja -C /r/mpv-build install

echo "=== Packaging single mpv player instance into 'MPV PLAYER AC-4' ==="
/r/msys64/ucrt64/bin/python.exe /r/agent/package_portable.py

echo "=== mpv build complete ==="
