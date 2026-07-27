#!/usr/bin/env bash
# apply-upstream-diff.sh <diff-file>...
#
# Applies upstream diffs with fuzz and enforces the invariant ad-hoc checking kept
# missing (the flight-14 retro): after every apply, ZERO new .rej files exist, or
# this script fails loudly listing them. Never pipe an apply to /dev/null again --
# use this instead. Run from src/zandronum.
set -u
fail=0
for d in "$@"; do
	before=$(find src wadsrc -name '*.rej' 2>/dev/null | sort)
	out=$(patch -p1 --no-backup-if-mismatch -F3 < "$d" 2>&1)
	rc=$?
	after=$(find src wadsrc -name '*.rej' 2>/dev/null | sort)
	newrej=$(comm -13 <(echo "$before") <(echo "$after"))
	if [ $rc -ne 0 ] || [ -n "$newrej" ]; then
		echo "APPLY-FAIL: $d (patch exit $rc)"
		[ -n "$newrej" ] && echo "$newrej" | sed 's/^/  reject: /'
		echo "$out" | grep -E 'FAILED|fuzz|offset' | head -5 | sed 's/^/  /'
		fail=1
	fi
done
exit $fail
