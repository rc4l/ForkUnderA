#!/usr/bin/env bash
# [rc4l] Compile ForkUnderA for the browser: the `web` preset (src/zandronum/CMakePresets.json) under the pinned Emscripten, into build-web/, staged to dist-web/.
#
#   EMSDK=/path/to/emsdk ./web_compile.sh
#
# Needs a native build first, for its host tools (zipdir, lemon, re2c, updaterevision): build-linux/,
# build-win/ or build/ beside this script, or the one NATIVE_BUILD names.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

: "${EMSDK:?set EMSDK to an emsdk checkout with 4.0.0 activated (tools/web/README.md)}"
# [rc4l] On Windows emsdk's own Python is a python.exe, which its shell setup does not look for.
if [ -z "${EMSDK_PYTHON:-}" ]; then
  for p in "$EMSDK"/python/*/python.exe; do [ -f "$p" ] && export EMSDK_PYTHON="$p"; done
fi
EMSDK_QUIET=1 source "$EMSDK/emsdk_env.sh"

NATIVE_BUILD="${NATIVE_BUILD:-}"
if [ -z "$NATIVE_BUILD" ]; then
  for d in build-linux build-win build; do
    [ -f "$d/ImportExecutables.cmake" ] && { NATIVE_BUILD="$d"; break; }
  done
fi
[ -n "$NATIVE_BUILD" ] && [ -f "$NATIVE_BUILD/ImportExecutables.cmake" ] \
  || { echo "no native build with ImportExecutables.cmake: build natively first, or set NATIVE_BUILD" >&2; exit 1; }

cmake -S src/zandronum --preset web -DIMPORT_EXECUTABLES="$ROOT/$NATIVE_BUILD/ImportExecutables.cmake"
cmake --build build-web ${ZX_KEEP_GOING:+-- -k 0}
node tools/web/stage.mjs build-web dist-web
