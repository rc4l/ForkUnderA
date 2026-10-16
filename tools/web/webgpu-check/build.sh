#!/usr/bin/env bash
# [rc4l] Builds the WebGPU check with the pinned Emscripten found through $EMSDK, into build-webgpu-check/ at the repo root.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
: "${EMSDK:?set EMSDK to an emsdk checkout with 4.0.0 activated (tools/web/README.md)}"
# [rc4l] On Windows emsdk's own Python is a python.exe, which its shell setup does not look for.
if [ -z "${EMSDK_PYTHON:-}" ]; then
  for p in "$EMSDK"/python/*/python.exe; do [ -f "$p" ] && export EMSDK_PYTHON="$p"; done
fi
EMSDK_QUIET=1 source "$EMSDK/emsdk_env.sh"
BUILD="$ROOT/build-webgpu-check"
[ -f "$BUILD/build.ninja" ] || emcmake cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target triangle shadercheck
echo "page: $BUILD/triangle.html   converter: node $BUILD/shadercheck.js <dumped shaders>"
