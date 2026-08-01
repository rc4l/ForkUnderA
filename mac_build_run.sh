#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# mac_build_run.sh -- fail-CLOSED incremental rebuild + bundle for ZandroX (macOS).
#
# THE sanctioned way to rebuild and relaunch after a code or wadsrc/ change. It
# replaces hand-rolling `cmake --build --target zdoom` + manual pk3/binary copies
# (the "three stale layers"), which silently ship STALE or MISSING artifacts and
# send you debugging the wrong layer -- a failed link that mac_compile.sh reports
# as "Done", a `Cannot find zandronum.pk3` at startup, an lldb spelunk to discover
# the running binary predates your edit. Every step here fails LOUD instead:
#
#   1. Build fails, or produces no binary  -> STOP. Never bundle a stale binary.
#   2. A pk3 is missing or older than its   -> repack it with zipdir. `--target zdoom`
#      wadsrc/ inputs                          never rebuilds pk3s: add_pk3's only
#                                              DEPENDS is the zipdir *tool*, not the
#                                              wadsrc content, and the pk3 ALL-target
#                                              is not a zdoom dependency -- so a
#                                              zdoom-only build leaves them stale, and
#                                              a deleted pk3 never comes back.
#   3. Bundle ends up without a pk3, or with -> STOP. This is the exact failure the
#      a binary/pk3 that doesn't match build/    screenshots showed reaching the engine.
#
# Only when every artifact is proven fresh and consistent does it say the bundle is
# safe to launch. Requires a bundle already assembled once by ./mac_compile.sh
# (dylibs, icon, Info.plist); this script only refreshes the binary + pk3s + seal.
#
#   ./mac_build_run.sh          # build, refresh bundle, verify, stop
#   ./mac_build_run.sh --run    # ... then launch the bundle
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
SRC="$ROOT/src/zandronum"
APP="$BUILD/ZandroX.app"
MACOS="$APP/Contents/MacOS"
ZIPDIR="$BUILD/tools/zipdir/zipdir"
BIN="$BUILD/zandronum"
NCPU="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

status() { printf '\033[32m==> %s\033[0m\n' "$*"; }
warn()   { printf '\033[33mwarning: %s\033[0m\n' "$*"; }
die()    { printf '\033[31mBUILD-RUN FAILED: %s\033[0m\n' "$*" >&2; exit 1; }

[ -f "$BUILD/CMakeCache.txt" ] || die "build/ is not configured -- run ./mac_compile.sh once first."
[ -x "$ZIPDIR" ]              || die "zipdir tool missing at $ZIPDIR -- run ./mac_compile.sh once first."
[ -d "$MACOS" ]              || die "no .app bundle at $APP -- run ./mac_compile.sh once to assemble it."

# --- 1. Build the binary. A failing or empty link is a HARD stop. --------------
# The screenshots' root cause: the link failed but the build was treated as
# "Done" and the stale binary got bundled. Here a non-zero build, or a build that
# leaves no binary, aborts before anything is copied.
status "Building zdoom (incremental, $NCPU jobs)..."
cmake --build "$BUILD" --target zdoom --parallel "$NCPU" \
  || die "the build failed (see the compile/link error above). NOT bundling -- fix it; do not run a stale bundle."
[ -x "$BIN" ] || die "the build reported success but produced no binary at $BIN (silent link failure)."

# --- 2. pk3 freshness. Repack any pk3 that is missing or older than wadsrc/. ---
# We own this because the zdoom target never rebuilds pk3s (see header).
pk3_pairs=( "wadsrc:zandronum.pk3" "wadsrc_bm:brightmaps.pk3"
            "wadsrc_lights:lights.pk3" "wadsrc_st:skulltag_actors.pk3" )
for pair in "${pk3_pairs[@]}"; do
    dir="${pair%%:*}"; name="${pair##*:}"
    srcdir="$SRC/$dir/static"; pk3="$BUILD/$name"
    [ -d "$srcdir" ] || continue
    reason=""
    if [ ! -f "$pk3" ]; then
        reason="missing"
    elif [ -n "$(find "$srcdir" -type f -newer "$pk3" 2>/dev/null | head -1 || true)" ]; then
        reason="stale vs $dir/static"
    fi
    if [ -n "$reason" ]; then
        status "Repacking $name ($reason)..."
        "$ZIPDIR" -udf "$pk3" "$srcdir" >/dev/null || die "zipdir failed to build $name."
    fi
    [ -f "$pk3" ] || die "$name still absent after repack -- zipdir produced nothing."
done

# --- 3. Sync into the bundle, VERIFY the copy is faithful, THEN re-seal. -------
status "Syncing fresh binary + pk3s into the bundle..."
cp -f "$BIN" "$MACOS/zandronum"; chmod u+w "$MACOS/zandronum"
for pk3 in "$BUILD"/*.pk3; do [ -e "$pk3" ] && cp -f "$pk3" "$MACOS/"; done

# Verify BEFORE signing -- codesign rewrites the bundle binary in place, so these
# byte-compares must run while it is still the freshly-copied artifact. These are
# the assertions that would have caught every failure in the screenshots:
[ -f "$MACOS/zandronum.pk3" ] \
  || die "the bundle has NO zandronum.pk3 after sync -- the engine would abort with 'Cannot find zandronum.pk3'."
cmp -s "$BIN" "$MACOS/zandronum" \
  || die "bundle binary != build/ binary (a stale copy slipped through)."
cmp -s "$BUILD/zandronum.pk3" "$MACOS/zandronum.pk3" \
  || die "bundle zandronum.pk3 != build/ zandronum.pk3 (a stale copy slipped through)."

# Refreshing the main executable invalidates the app's signature; deep-sign
# re-seals the whole bundle including the executable (this is expected to change
# the on-disk binary -- which is why the compares above ran first).
if command -v codesign >/dev/null 2>&1; then
    codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || warn "deep re-sign of bundle failed (Gatekeeper may block launch)"
fi

status "OK -- fresh binary + pk3s verified in the bundle. Safe to launch."
if [ "${1:-}" = "--run" ]; then
    status "Launching $APP ..."
    open "$APP"
fi
