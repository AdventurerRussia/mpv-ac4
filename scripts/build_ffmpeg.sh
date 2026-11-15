#!/usr/bin/env bash
set -e
cd /r/src/ffmpeg-upstream

echo "=== Running Upstream FFmpeg configure ==="
./configure \
    --prefix=/r/install \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-debug \
    --enable-gpl \
    --enable-version3

echo "=== Running make ==="
make -j$(nproc)

echo "=== Running make install ==="
make install

echo "=== Upstream FFmpeg build and install complete ==="

