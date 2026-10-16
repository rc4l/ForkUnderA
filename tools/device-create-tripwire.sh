#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Device-creation tripwire: the backend makes its textures and buffers through CreateDeviceTexture and CreateDeviceBuffer (dgprobe.cpp), which keep them for WebGPU to destroy; one made on the device directly is left to a browser's garbage collector, which a page resized a few times outran until it was out of GPU memory.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src/zandronum/src"
ALLOWED="features/hwrender/diligent/dgprobe.cpp"

# [rc4l] Call syntax only, past the grep prefix, so prose in comments stays legal.
hits=$(cd "$SRC" && grep -rnE -- '->Create(Texture|Buffer)\(' features \
    --include='*.cpp' --include='*.h' --include='*.mm' 2>/dev/null \
    | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' \
    | grep -vF "$ALLOWED:")

if [ -n "$hits" ]; then
    echo "DEVICE-CREATE TRIPWIRE: textures or buffers made on the device directly; use CreateDeviceTexture or CreateDeviceBuffer:" >&2
    echo "$hits" >&2
    exit 1
fi
echo "device-create-tripwire: clean."
exit 0
