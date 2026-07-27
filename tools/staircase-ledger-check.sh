#!/usr/bin/env bash
# staircase-ledger-check.sh -- CI guard for the renderer-staircase ledger.
#
# Enforces two invariants:
#  1. No silent skips: every commit at or behind the frontier is resolved
#     (not 'pending'). The frontier is the newest non-pending row's sha.
#  2. Provenance: every 'ported'/'adapted' row names a zandrox_sha that exists
#     in our git history (guards against typos and dangling references).
set -u
cd "$(dirname "$0")/.."
LEDGER=progress/renderer-staircase/ledger.tsv
[ -f "$LEDGER" ] || { echo "ledger-check: $LEDGER missing"; exit 1; }

fail=0

# Invariant 1: find the last non-pending row; nothing before it may be pending.
# (Rows are chronological, so a pending row above a resolved row is a silent gap.)
last_resolved=$(awk -F'\t' 'NR>1 && $4!="pending"{n=NR} END{print n}' "$LEDGER")
gap=$(awk -F'\t' -v last="$last_resolved" 'NR>1 && NR<last && $4=="pending"{print $1}' "$LEDGER")
if [ -n "$gap" ]; then
	echo "ledger-check: pending commits behind the frontier (silent-skip guard):"
	echo "$gap" | sed 's/^/  /'
	fail=1
fi

# Invariant 2: zandrox_sha of resolved rows must resolve in our history.
while IFS=$'\t' read -r sha date cat status zx note; do
	[ "$status" = "ported" ] || [ "$status" = "adapted" ] || continue
	[ "$zx" = "—" ] && { echo "ledger-check: $sha is $status but has no zandrox_sha"; fail=1; continue; }
	git cat-file -e "$zx^{commit}" 2>/dev/null || { echo "ledger-check: $sha -> zandrox_sha $zx does not exist"; fail=1; }
done < <(tail -n +2 "$LEDGER")

[ "$fail" -eq 0 ] && echo "staircase-ledger-check: clean ($(( $(wc -l < "$LEDGER") - 1 )) rows)."
exit $fail
