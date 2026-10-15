#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Telemetry tripwire: every counter features/telemetry registers is named "group/name" in lower case, given as a string literal, no two ZX_ZONE sites share a name, which would add two different scopes into one timing, and every zone perf.ticprof reads is still timed.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_SRC="$ROOT/src/zandronum/src"
NAME='[a-z0-9_]+(/[a-z0-9_]+)+'
failed=0

# [rc4l] Every ZX_ZONE, ZX_COUNT and CounterSpec occurrence on its own line, prefixed with its file and line, outside comments and the macros' own definitions.
sites=$(grep -rnoE '(ZX_ZONE|ZX_COUNT)[[:space:]]*\([^)]*|CounterSpec\{[^,}]*' "$ENGINE_SRC" --include='*.cpp' --include='*.h' 2>/dev/null \
    | grep -vE '^[^:]*features/telemetry/telemetry\.h:')
comments=$(grep -rnE '^[[:space:]]*(//|\*|/\*)' "$ENGINE_SRC" --include='*.cpp' --include='*.h' 2>/dev/null | cut -d: -f1,2)
if [ -n "$comments" ]; then sites=$(echo "$sites" | grep -vF -f <(echo "$comments" | sed 's/$/:/')); fi

# [rc4l] A CounterSpec may take its name from a variable, which names it where the variable is set, or build it on a literal "group/" prefix.
bad=$(echo "$sites" | grep -vE ":(ZX_ZONE|ZX_COUNT)[[:space:]]*\([[:space:]]*\"$NAME\"|:CounterSpec\{[[:space:]]*(\"$NAME\"|[A-Za-z_][A-Za-z0-9_.]*[[:space:]]*$|\(std::string\(\"[a-z0-9_]+/\"\))" | grep -v '^$')
if [ -n "$bad" ]; then
    echo "TELEMETRY TRIPWIRE: counters not named by a \"group/name\" literal in lower case:" >&2
    echo "$bad" >&2
    failed=1
fi

dupes=$(echo "$sites" | grep -oE "ZX_ZONE[[:space:]]*\([[:space:]]*\"$NAME\"" | grep -oE "\"$NAME\"" | sort | uniq -d)
if [ -n "$dupes" ]; then
    echo "TELEMETRY TRIPWIRE: ZX_ZONE names used at more than one site:" >&2
    echo "$dupes" >&2
    failed=1
fi

# [rc4l] The bridge's perf.ticprof reads zones by name (kTicZones in ticprof_compute.cpp), so each must still be timed at a site.
ticzones=$(grep -oE 'kTicZones\[kTicZoneCount\] = \{[^}]*\}' "$ENGINE_SRC/features/mcp-bridge/computation/ticprof_compute.cpp" 2>/dev/null | grep -oE "\"$NAME\"")
if [ -z "$ticzones" ]; then
    echo "TELEMETRY TRIPWIRE: cannot read perf.ticprof's zone names from ticprof_compute.cpp" >&2
    failed=1
fi
for zone in $ticzones; do
    if ! echo "$sites" | grep -qE "ZX_ZONE[[:space:]]*\([[:space:]]*$zone"; then
        echo "TELEMETRY TRIPWIRE: perf.ticprof reads $zone, which no ZX_ZONE times" >&2
        failed=1
    fi
done

[ "$failed" -eq 0 ] && echo "zone-tripwire: clean."
exit "$failed"
