#!/usr/bin/env bash
# apply-upstream-diff.sh <diff-file>...
#
# Applies upstream diffs with fuzz and enforces the invariant ad-hoc checking kept
# missing (the flight-14 retro): after every apply, ZERO new .rej files exist, or
# this script fails loudly listing them. Never pipe an apply to /dev/null again --
# use this instead. Run from src/zandronum.
#
# [rc4l] CR stripping below is not cosmetic -- it is the difference between "this commit does
# not apply at all" and "this commit applies cleanly". Upstream's source files are CRLF; ours
# are LF. A diff taken from upstream therefore carries CRLF on its context lines, so NOTHING
# matches our files and patch reports every hunk of every file as rejected. That looks exactly
# like massive divergence and invites the wrong conclusion -- hand-port it, or skip it as
# "diverged too far". Seen for real on the 2014-05 batch: an 11-file refactor reported as fully
# rejected, including a single-hunk file whose context was byte-identical to ours; with CR
# stripped the same patch came down to two trivial rejects. Done here so no caller has to
# remember, and so a bare `patch < foo.diff` is never the tempting shortcut.
set -u
fail=0
for d in "$@"; do
	before=$(find src wadsrc -name '*.rej' 2>/dev/null | sort)
	out=$(tr -d '\r' < "$d" | patch -p1 --no-backup-if-mismatch -F3 2>&1)
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
