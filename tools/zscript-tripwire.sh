#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] ZScript tripwire: fails if VM/ZScript symbols appear in code the engine compiles.
# Policy: docs/zscript-insulation.md. ZandroX must never link the ZScript VM -- the netcode
# replicates a closed, hand-instrumented function set (docs/zscript-feasibility.md), so any VM
# symbol entering the build is an error, not a style issue.
#
# Usage:
#   tools/zscript-tripwire.sh              # scan the tree (CI / pre-merge)
#   tools/zscript-tripwire.sh <paths...>   # scan specific files (pre-batch cherry-pick audit)
#
# The vendored reference trees (src/zandronum/rendering, src/zandronum/ZVulkan) are exempt: they
# are not in the engine's source list. If they ever enter the build, they must pass this gate.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_SRC="$ROOT/src/zandronum/src"

# [rc4l] ZScript-ERA-ONLY symbols, calibrated to zero hits on the clean tree. Two traps this
# deliberately avoids: GC::WriteBarrier is ZDoom's 2008 DObject garbage collector (ours, native,
# pre-ZScript), and DEFINE_ACTION_FUNCTION is the classic DECORATE codepointer macro whose NAME
# ZScript later reused -- both are legitimate in this engine and must not trip the wire.
PATTERN='VMFunction|VMValue|VMFrameStack|VMNativeFunction|IFVIRTUAL|PARAM_SELF_PROLOGUE|ZCC_|GlobalVMStack|scripting/vm|zscript'

# Paths the engine actually compiles (vendored reference trees excluded by construction).
if [ "$#" -gt 0 ]; then
    TARGETS=("$@")
else
    TARGETS=("$ENGINE_SRC")
fi

# [rc4l] Allowlist: pre-existing engine identifiers that merely collide with the pattern's spirit.
# Keep this list SHORT and reviewed; every entry is a place a curve ball could hide.
ALLOW_FILE_RE='(^|/)(docs|tools)(/|$)'

hits=$(grep -rnE "$PATTERN" "${TARGETS[@]}" \
    --include='*.cpp' --include='*.h' --include='*.c' --include='*.txt' 2>/dev/null \
    | grep -vE "$ALLOW_FILE_RE" \
    | grep -vE '// *\[rc4l\].*(tripwire|zscript)' )

if [ -n "$hits" ]; then
    echo "ZSCRIPT TRIPWIRE: VM/ZScript symbols found in compiled engine code:" >&2
    echo "$hits" >&2
    echo >&2
    echo "Policy: docs/zscript-insulation.md -- strip the VM surface or reimplement (see the" >&2
    echo "F2DDrawer precedent: script exports are typically <6% of a file and self-contained)." >&2
    exit 1
fi
echo "zscript-tripwire: clean."
exit 0
