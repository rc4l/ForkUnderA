#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Backport scout: the automated front half of the engine back-translation workflow
# (docs/engine-zscript-workflow.md). Given an upstream file, it answers in one run:
#   1. Is it VM/ZScript-tainted? (tripwire)
#   2. If it is ZScript (or scriptified): which commit scriptified it, and which C++ ancestor
#      files hold the original? (Rosetta index)
#   3. What changed since scriptification? (the delta a human translates)
#
# Usage: tools/backport-scout.sh <upstream-clone> <path/in/upstream>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UP="${1:?usage: backport-scout.sh <upstream clone> <upstream path>}"
TARGET="${2:?usage: backport-scout.sh <upstream clone> <upstream path>}"
ROSETTA="$ROOT/tools/data/zscript-rosetta.tsv"

echo "== backport scout: $TARGET =="

echo "-- 1. tripwire --"
if [ -f "$UP/$TARGET" ]; then
    if "$ROOT/tools/zscript-tripwire.sh" "$UP/$TARGET" >/dev/null 2>&1; then
        echo "CLEAN: no VM/ZScript symbols; candidate for direct adaptation (audit fixed_t + seams)."
    else
        echo "TAINTED: VM/ZScript symbols present; back-translation required (see below)."
    fi
else
    echo "note: file not present at upstream HEAD (renamed or ZScript-only path)."
fi

echo "-- 2. rosetta lookup --"
base=$(basename "$TARGET" | sed 's/\.[a-z]*$//')
hits=$(grep -i "$base" "$ROSETTA" 2>/dev/null || true)
if [ -n "$hits" ]; then
    echo "$hits" | while IFS=$'\t' read -r h d cpp zs subj; do
        echo "scriptified in $h ($d): $subj"
        echo "  C++ ancestor files (at $h~1): $cpp"
        echo "  ZScript outputs: $zs"
    done
else
    echo "no scriptification record for '$base' -- either never scriptified (pure C++ history) or born as ZScript."
fi

echo "-- 3. delta since scriptification (translate this) --"
first=$(echo "$hits" | head -1 | cut -f1)
if [ -n "${first:-}" ] && [ -f "$UP/$TARGET" ]; then
    (cd "$UP" && git diff --stat "$first" HEAD -- "$TARGET" | tail -3)
    echo "(full diff: cd $UP && git diff $first HEAD -- $TARGET)"
else
    echo "n/a"
fi
