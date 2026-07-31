#!/usr/bin/env bash
# commit-tracker-check.sh -- CI guard for commit-tracker/coverage.tsv.
#
# Keeps the tracker machine-parseable and honest:
#  1. Format: every data row is exactly 5 tab-separated fields, a 40-hex sha, and a
#     status in the vocabulary {pending, ported, adapted, skip}.
#  2. Provenance: every 'ported'/'adapted' row names its zandrox commit in the note,
#     and if that first token looks like a sha it must exist in our history.
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
  { if (NF!=5)                        { print "  NF="NF": "substr($0,1,60); next }
    if ($1 !~ /^[0-9a-f]{40}$/)       { print "  bad sha: "$1; next }
    if ($4!="pending" && $4!="ported" && $4!="adapted" && $4!="skip")
                                      { print "  bad status: "$4" ("$1")" } }' "$TSV")
if [ -n "$badfmt" ]; then echo "commit-tracker-check: format errors:"; echo "$badfmt"; fail=1; fi

while IFS=$'\t' read -r sha date title status note; do
	case "$status" in ported|adapted) ;; *) continue ;; esac
	tok=${note%% *}
	if [ -z "$tok" ]; then
		echo "commit-tracker-check: $sha is $status but note names no provenance commit"; fail=1; continue
	fi
	if printf '%s' "$tok" | grep -qE '^[0-9a-f]{7,40}$'; then
		git cat-file -e "$tok^{commit}" 2>/dev/null || { echo "commit-tracker-check: $sha -> note sha $tok does not exist"; fail=1; }
	fi
done < <(awk -F'\t' '/^#/ || $1=="sha" {next} 1' "$TSV")

[ "$fail" -eq 0 ] && echo "commit-tracker-check: clean ($(( $(grep -c "" "$TSV") - 2 )) rows)."
exit $fail
