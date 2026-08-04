#!/usr/bin/env bash
#
# Compute port progress toward the current goal and write progress.json.
#
# The number is computed HERE, in the repo, and shipped as a small static file. The viewer
# only draws it — it never counts rows itself. That keeps the bar instant (progress.json is
# a few hundred bytes and paints long before the ~5 MB of TSV the table needs) and keeps one
# definition of "progress" instead of one per client.
#
# Progress = commits that are NOT `pending`, over every commit up to the goal.
# `skip` counts as done: a reviewed-and-rejected commit is handled, not outstanding.
# `deferred` counts as done too: it is a REVIEWED decision (policy says not now), not a backlog
# item. It differs from skip in being revisitable -- see the ZScript vocabulary note in
# .claude/skills/sequential-backport/SKILL.md.
#
# ---- THE GOAL: edit these two lines to retarget the bar, then re-run. ----
GOAL_LABEL="${GOAL_LABEL:-GZDoom 2.0.05}"   # what we're aiming at, shown next to the bar
GOAL_DATE="${GOAL_DATE:-2014-12-27}"        # its release date; rows dated <= this are in scope
#
# The boundary is a date because coverage.tsv is chronological and carries a date per row, so
# no extra data is needed to place it. Past goals, for the record:
#   GZDoom 2.0.05  2014-12-27   (current)
#   GZDoom 2.1.1   2016-02-23   (next; renderer half is gated on the base-engine backport, #41)
#
# Usage:  ./progress.sh          # run standalone, or automatically at the end of regen.sh
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TSV="$DIR/coverage.tsv"
OUT="$DIR/progress.json"

[ -f "$TSV" ] || { echo "progress.sh: $TSV not found — run regen.sh first" >&2; exit 1; }

# Counted in one pass. `done` is every non-pending status so the definition can't drift out of
# sync with the status vocabulary: add a new status and it counts as handled automatically.
awk -F'\t' -v label="$GOAL_LABEL" -v goal="$GOAL_DATE" -v gen="$(date -u +%Y-%m-%d)" '
  $1 == "sha" || $1 ~ /^#/ || NF < 4 { next }
  {
    if (substr($2, 1, 10) > goal) next          # past the goal: out of scope for this bar
    total++
    if ($4 != "pending") { done++; by[$4]++ }
  }
  END {
    if (total == 0) { print "progress.sh: no rows at or before " goal > "/dev/stderr"; exit 1 }
    printf "{\n"
    printf "  \"goal\": \"%s\",\n", label
    printf "  \"goal_date\": \"%s\",\n", goal
    printf "  \"done\": %d,\n", done + 0
    printf "  \"total\": %d,\n", total
    printf "  \"pct\": %.1f,\n", 100 * (done + 0) / total
    printf "  \"ported\": %d,\n", by["ported"] + 0
    printf "  \"adapted\": %d,\n", by["adapted"] + 0
    printf "  \"skip\": %d,\n", by["skip"] + 0
    printf "  \"deferred\": %d,\n", by["deferred"] + 0
    printf "  \"generated\": \"%s\"\n", gen
    printf "}\n"
  }' "$TSV" > "$OUT"

cat "$OUT"
