#!/usr/bin/env bash
# [rc4l] Compile ForkUnderA for the browser: the `web` preset (src/zandronum/CMakePresets.json) under the pinned Emscripten, into build-web/, staged to dist-web/.
#
#   EMSDK=/path/to/emsdk ./web_compile.sh
#   ZX_WITH_SYMBOLS=1 JOBS=8 ZX_KEEP_GOING=1 ./web_compile.sh   # named stacks, 8 compilers, every error at once
#
# Uses a native build's host tools (zipdir, lemon, re2c, updaterevision) from build-linux/,
# build-win/ or build/ beside this script, or the one NATIVE_BUILD names, else builds them alone.
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
  for d in build-linux build-win build build-hosttools; do
    [ -f "$d/ImportExecutables.cmake" ] && { NATIVE_BUILD="$d"; break; }
  done
fi
# [rc4l] No native build here: the tools alone, built for this machine (tools/web/hosttools).
if [ -z "$NATIVE_BUILD" ]; then
  cmake -S tools/web/hosttools -B build-hosttools -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build-hosttools
  NATIVE_BUILD=build-hosttools
fi

# [rc4l] ZX_WITH_SYMBOLS=1 keeps the function names in the wasm, so a crash's stack in the browser names code rather than wasm-function[N].
SYM_ARGS=( -DNO_STRIP=OFF "-DCMAKE_EXE_LINKER_FLAGS=" )
if [ "${ZX_WITH_SYMBOLS:-0}" = "1" ]; then SYM_ARGS=( -DNO_STRIP=ON "-DCMAKE_EXE_LINKER_FLAGS=--profiling-funcs" ); fi

cmake -S src/zandronum --preset web -DIMPORT_EXECUTABLES="$ROOT/$NATIVE_BUILD/ImportExecutables.cmake" "${SYM_ARGS[@]}"
# [rc4l] JOBS caps the compile, to leave the machine to something else running.
cmake --build build-web ${JOBS:+-j "$JOBS"} ${ZX_KEEP_GOING:+-- -k 0}
node tools/web/stage.mjs build-web dist-web
