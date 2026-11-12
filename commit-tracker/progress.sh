#!/usr/bin/env bash
#
# Compute port progress toward each ledger's goal and write its progress.json.
#
# The number is computed HERE, in the repo, and shipped as a small static file. The viewer only
# draws it -- it never counts rows itself, so the bar paints long before the TSVs the table needs.
#
# Progress = rows that are NOT `pending`, from the top of coverage.tsv down to the goal ROW. `skip`
# and `deferred` count as done: both are reviewed decisions, not backlog (see the vocabulary in
# .claude/skills/sequential-backport/SKILL.md). The goal is a row, not a date, because Zandronum's
# ledger is in revision order and its transplants keep their 2014-2018 dates.
#
# ---- THE GOAL lives in ledgers.json: "goal": {"label", "sha", "date"} ("sha": "tip" = the last row).
# Past UZDoom goals, for the record:
#   GZDoom 2.0.05  2014-12-27   reached 2026-08-04 (1179/1179)
#   GZDoom 2.1.1   2016-02-23   (current, row 1f09341d2) -- chosen over 2.3 so the sim lands where the
#                               renderer staircase already stopped (db766f9fe, 2016-02-03) and near the
#                               Cocoa backend target (108dcf122, 2016-09), rather than leaving three
#                               subsystems at three different points in upstream history.
#   GZDoom 2.3     2016-09-19   (after) -- the LAST release before ZScript exists. ZScript's language
#                               commit is 433bf4601 (2016-10-13): g2.3pre does not contain it, g2.4pre does.
#
# Usage:  ./progress.sh [--ledger NAME]     # every ledger by default; regen.sh runs it too
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$DIR/python.sh"
exec "${PY[@]}" "$DIR/tracker.py" progress "$@"
