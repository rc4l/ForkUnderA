#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Regenerates the ZScript Rosetta index: every upstream "scriptified" commit mapped to the
# C++ files it consumed and the ZScript files it produced. This is the lookup table for the
# engine-side back-translation workflow (docs/engine-zscript-workflow.md): for any upstream feature
# now living in ZScript, the index names the commit whose parent still holds the C++ original.
#
# Usage: tools/zscript-rosetta-gen.sh /path/to/UZDoom-or-GZDoom-clone > tools/data/zscript-rosetta.tsv

set -euo pipefail
UPSTREAM="${1:?usage: zscript-rosetta-gen.sh <upstream clone path>}"

cd "$UPSTREAM"
echo -e "commit\tdate\tcpp_touched\tzscript_added\tsubject"
git log --reverse -i --grep="scriptif" --format="%H|%cs|%s" | while IFS='|' read -r h d s; do
    files=$(git show --name-status --format= "$h" 2>/dev/null)
    cpp=$(echo "$files" | grep -E '^[MD][[:space:]]+src/.*\.(cpp|h)$' | awk '{print $2}' | paste -sd, - || true)
    zs=$(echo "$files" | grep -E '^A[[:space:]]+wadsrc/static/zscript/' | awk '{print $2}' | paste -sd, - || true)
    # Only rows that actually moved code are useful lookups.
    if [ -n "$cpp" ] || [ -n "$zs" ]; then
        printf "%s\t%s\t%s\t%s\t%s\n" "$h" "$d" "${cpp:--}" "${zs:--}" "$(echo "$s" | cut -c1-100)"
    fi
done
