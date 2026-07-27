#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Removed-API tripwire: fails if compiled engine code calls an API a staircase flight has
# removed. Born from a real slip: Flight 1 removed GLU, but mac/linux still had GLU declarations in
# the platform header soup and CMake's OPENGL_LIBRARIES still linked it -- so a missed
# gluPerspective call compiled everywhere except Windows. Dependency removal is only as strict as
# the loosest platform; this makes the strictness uniform and mechanical.
#
# EVERY flight that removes an API appends its call-syntax pattern here.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_SRC="$ROOT/src/zandronum/src"

# Flight 1: GLU is gone (gluPerspective -> glFrustum; gluScaleImage -> imageresize_compute).
# Patterns require call syntax '(' so prose mentions in comments stay legal.
PATTERN='glu[A-Z][A-Za-z]*\('

# Comment lines are legal (grep output is file:line:content, so anchor past the prefix).
hits=$(grep -rnE "$PATTERN" "$ENGINE_SRC" \
    --include='*.cpp' --include='*.h' --include='*.c' 2>/dev/null \
    | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' )

if [ -n "$hits" ]; then
    echo "REMOVED-API TRIPWIRE: calls to APIs a staircase flight removed:" >&2
    echo "$hits" >&2
    exit 1
fi
echo "removed-api-tripwire: clean."
exit 0
