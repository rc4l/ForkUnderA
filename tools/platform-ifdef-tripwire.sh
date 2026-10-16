#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Platform-branch tripwire: the browser build's conditions (__EMSCRIPTEN__, PLATFORM_WEB), and NO_GL in feature code, stay in the named seam files below, so a port grows seams rather than a condition in every file.
#
# [rc4l] A new seam is added here on purpose, with the reason in its commit; anywhere else, put the platform's half behind its own file chosen by CMake.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/src/zandronum/src"

WEB_SEAMS="
cmdlib.cpp
features/hwrender/diligent/dgdevice_wgpu.cpp
features/hwrender/diligent/dgwindow.cpp
menu/menudef.cpp
resourcefiles/file_directory.cpp
sound/music_midi_timidity.cpp
"

NOGL_FEATURE_SEAMS="
features/crosshair/crosshair.cpp
features/damage-tint/damagetint.cpp
features/damage-tint/damagetint.h
features/frame/standaloneframe.cpp
features/fua-caching/fua_warmup.cpp
"

# [rc4l] Files whose preprocessor lines test `pattern`, relative to SRC, one per line.
files_testing() {
  ( cd "$SRC" && grep -rlE "^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif).*($1)" $2 \
      --include='*.cpp' --include='*.h' --include='*.c' --include='*.mm' 2>/dev/null ) | sed 's|^\./||' | sort
}

failed=0
check() {
  local what="$1" allowed="$2"; shift 2
  local f
  while IFS= read -r f; do
    [ -z "$f" ] && continue
    if ! printf '%s\n' "$allowed" | grep -qxF "$f"; then
      echo "PLATFORM TRIPWIRE: $f branches on $what outside the named seams (tools/platform-ifdef-tripwire.sh)." >&2
      failed=1
    fi
  done
}

check "the browser build" "$WEB_SEAMS" < <(files_testing '__EMSCRIPTEN__|PLATFORM_WEB' .)
check "NO_GL" "$NOGL_FEATURE_SEAMS" < <(files_testing '\bNO_GL\b' features)

[ "$failed" -eq 0 ] || exit 1
echo "platform-ifdef-tripwire: clean."
exit 0
