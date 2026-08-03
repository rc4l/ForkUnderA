#!/usr/bin/env bash
# commit-tracker-check.sh -- CI guard for commit-tracker/coverage.tsv.
#
# Keeps the tracker machine-parseable and honest:
#  1. Format: every data row is exactly 6 tab-separated fields, a 40-hex sha (THEIR
#     commit), and a status in the vocabulary {pending, ported, adapted, skip}.
#  2. Provenance (the `ours` column, field 6 = OUR repo commit(s) that addressed it):
#       - pending / skip   -> must be "/"  (inapplicable; nothing of ours addressed it)
#       - ported / adapted -> must be "zandronum-base" (base-inherited / adapted-present),
#                             or one-or-more comma-separated zandrox shas, each of which
#                             must exist in our history. This is what makes "which of our
#                             commits addressed it" enforceable instead of parsed from prose.
#
# (There is deliberately no "no pending behind the frontier" rule -- unlike the old
# staircase ledger, this tracker is cherry-picked scatter-wise, so pending rows among
# resolved ones are normal, not a silent gap.)
set -u
cd "$(dirname "$0")/.."
TSV=commit-tracker/coverage.tsv
[ -f "$TSV" ] || { echo "commit-tracker-check: $TSV missing"; exit 1; }

fail=0

badfmt=$(awk -F'\t' '
  /^#/ { next }
  $1=="sha" && $2=="date" { next }
  { if (NF!=6)                        { print "  NF="NF": "substr($0,1,60); next }
    if ($1 !~ /^[0-9a-f]{40}$/)       { print "  bad sha: "$1; next }
    if ($4!="pending" && $4!="ported" && $4!="adapted" && $4!="skip")
                                      { print "  bad status: "$4" ("$1")"; next }
    if (($4=="pending" || $4=="skip") && $6!="/")
                                      { print "  "$4" row must have ours=/ : "$1" (ours="$6")" } }' "$TSV")
if [ -n "$badfmt" ]; then echo "commit-tracker-check: format errors:"; echo "$badfmt"; fail=1; fi

# Provenance: ported/adapted must name a real our-commit (or "zandronum-base").
while IFS=$'\t' read -r sha date title status note ours; do
	case "$status" in ported|adapted) ;; *) continue ;; esac
	if [ -z "$ours" ]; then
		echo "commit-tracker-check: $sha is $status but ours column is empty"; fail=1; continue
	fi
	[ "$ours" = "zandronum-base" ] && continue
	# one-or-more comma-separated zandrox shas; every one must be REACHABLE FROM HEAD.
	#
	# [rc4l] Reachability, not mere existence. `git cat-file -e` is satisfied by any object still in
	# the local store, including commits nothing points at any more -- so a row citing an orphaned
	# commit passes on the machine that orphaned it and fails in CI's fresh clone, which is exactly
	# how this shipped red once. Two ways rows get orphaned, both seen in practice:
	#   - `git commit --amend` after capturing the sha, so the row cites the pre-amend commit;
	#   - squash-merging a PR, which collapses every commit its own rows cite (PR #139 did this to
	#     seven batch-1 rows). Backport PRs should merge with a merge commit for this reason.
	IFS=',' read -ra parts <<< "$ours"
	for tok in "${parts[@]}"; do
		if ! printf '%s' "$tok" | grep -qE '^[0-9a-f]{7,40}$'; then
			echo "commit-tracker-check: $sha ours='$ours' -> '$tok' is not a sha or 'zandronum-base'"; fail=1; continue
		fi
		if ! git cat-file -e "$tok^{commit}" 2>/dev/null; then
			echo "commit-tracker-check: $sha -> our commit $tok does not exist"; fail=1
		elif ! git merge-base --is-ancestor "$tok" HEAD 2>/dev/null; then
			echo "commit-tracker-check: $sha -> our commit $tok is not reachable from HEAD (orphaned by an amend or a squash merge?)"; fail=1
		fi
	done
done < <(awk -F'\t' '/^#/ || $1=="sha" {next} 1' "$TSV")

# [rc4l] Cross-check that a ported/adapted row's cited commit plausibly IS that port -- this script
# only proves the sha exists and is reachable, both of which a mis-recorded row satisfies happily.
python3 "$(dirname "$0")/commit-tracker-overlap.py" || fail=1

# [rc4l] progress.json is a static file the dashboard draws without counting rows itself, and
# nothing regenerates it when coverage.tsv is edited by hand -- which is how the published bar came
# to read 26.5% while the ledger said 47.6%, stale by two months and a hundred-odd decided commits.
# A wrong number shown publicly is worse than no number, so it fails here rather than drifting.
PROGRESS="$(dirname "$0")/../commit-tracker/progress.json"
if [ -f "$PROGRESS" ]; then
	goal_date=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['goal_date'])" "$PROGRESS")
	# Counted here rather than by calling progress.sh, which WRITES the file it would be checked
	# against -- running it would make the comparison trivially pass, every time.
	want=$(awk -F'\t' -v goal="$goal_date" '
		$1=="sha" || $1 ~ /^#/ || NF < 4 { next }
		{ if (substr($2,1,10) > goal) next; total++; if ($4 != "pending") done++ }
		END { printf "%d %d", done+0, total+0 }' "$TSV")
	have=$(python3 -c "import json,sys;d=json.load(open(sys.argv[1]));print(d['done'],d['total'])" "$PROGRESS")
	if [ "$want" != "$have" ]; then
		echo "commit-tracker-check: progress.json is stale (says '$have' done/total, coverage.tsv says '$want') -- run commit-tracker/progress.sh"
		fail=1
	fi
fi

[ "$fail" -eq 0 ] && echo "commit-tracker-check: clean ($(( $(grep -c "" "$TSV") - 2 )) rows)."
exit $fail
