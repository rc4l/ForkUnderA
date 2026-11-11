#!/usr/bin/env bash
# commit-tracker-check.sh -- CI guard for every ledger in commit-tracker/ledgers.json.
#
# For each ledger (commit-tracker/tracker.py check does the work):
#  1. Format: every row has the ledger's columns, a 40-hex sha, a status in the vocabulary
#     {pending, ported, adapted, skip, deferred, partially-deferred}, and no duplicate rows.
#  2. Provenance (`ours`, OUR repo commit(s) that addressed the row): pending/skip must be "/";
#     ported/adapted/partially-deferred must be "zandronum-base" (where the ledger allows it) or
#     comma-separated shas of ours, each REACHABLE FROM HEAD. Reachability, not existence: an amend
#     or a squash merge leaves the cited commit in the local store but nowhere in CI's clone, which
#     is how this shipped red once (PR #139 squashed seven batch-1 rows away). Backport PRs merge
#     with a merge commit for this reason.
#  3. Twins: a Zandronum row transplanted from UZDoom names its UZDoom commit, and both rows must
#     agree (same status, same ours).
#  4. Overlap: a ported/adapted row's cited commit shares a file with the upstream commit
#     (commit-tracker-overlap.py explains why and when `no-file-overlap:` is the escape).
#  5. progress.json matches the rows, so the published bar can never go stale from a hand edit.
#
# There is deliberately no "no pending behind the frontier" rule: ledgers are walked scatter-wise
# (twins land at their Zandronum position, fixes get pulled forward), so pending rows among
# resolved ones are normal, not a silent gap.
set -u
DIR="$(cd "$(dirname "$0")/.." && pwd)"
. "$DIR/commit-tracker/python.sh"
exec "${PY[@]}" "$DIR/commit-tracker/tracker.py" check "$@"
