#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] PowerShell encoding tripwire: fails if a .ps1 contains non-ASCII bytes without a UTF-8 BOM.
#
# Born from a real break: windows_compile.ps1 and windows_build.ps1 both carried em-dashes and were
# saved BOM-less. Windows PowerShell 5.1 -- still the default shell on a stock Windows box -- decodes
# a BOM-less .ps1 as ANSI/CP1252, so the em-dash bytes (E2 80 94) mis-decode into a curly quote
# (U+201D) that PowerShell treats as a string delimiter. That unbalances the file and the script
# fails to PARSE, emitting a wall of unrelated syntax errors that points nowhere near the real cause.
#
# CI never caught it because our workflows run `shell: pwsh` (PowerShell 7), which reads .ps1 as
# UTF-8 regardless of BOM. So this is invisible to CI by construction and only ever bites a
# contributor on their own machine -- exactly the class of bug a tripwire is for.
#
# The rule is narrow on purpose: ASCII-only files need no BOM, and files that want non-ASCII (the
# em-dashes read better) just need to be saved as UTF-8 WITH BOM, which pwsh and CI read identically.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

fail=0
while IFS= read -r f; do
	[ -f "$f" ] || continue

	# LC_ALL=C so the byte class means bytes, not multibyte characters.
	if ! LC_ALL=C grep -q $'[\x80-\xFF]' "$f" 2>/dev/null; then
		continue	# pure ASCII: decodes identically under every codepage, BOM irrelevant.
	fi

	bom=$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')
	if [ "$bom" != "efbbbf" ]; then
		echo "PS-ENCODING-FAIL: $f has non-ASCII bytes but no UTF-8 BOM"
		echo "  -> Windows PowerShell 5.1 will mis-decode it as ANSI and the script will not parse."
		echo "  -> Fix: save as 'UTF-8 with BOM', or make the file pure ASCII."
		fail=1
	fi
done < <(git ls-files '*.ps1')

if [ "$fail" -ne 0 ]; then
	exit 1
fi

echo "powershell-encoding-tripwire: OK (every non-ASCII .ps1 carries a UTF-8 BOM)"
