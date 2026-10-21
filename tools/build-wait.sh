#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# build-wait.sh -- the ONE sanctioned way to wait for mac_compile.sh.
#
# [rc4l] Exists because the obvious alternative is a trap: `while pgrep -f mac_compile.sh` run from
# a shell whose own command line contains "mac_compile.sh" matches ITSELF and spins forever --
# which once left six watcher shells mutually detecting each other while the build they "watched"
# had already failed. This reads the .build-status protocol mac_compile.sh maintains instead:
# no process-name matching anywhere, and a build that dies without writing a result (kill -9,
# power loss) is detected by its recorded pid going away.
#
# Exit codes: 0 = build succeeded; the build's own exit code if it failed;
#             3 = no build has ever run; 4 = builder vanished without a result.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATUS_FILE="$ROOT/.build-status"

if [[ ! -f "$STATUS_FILE" ]]; then
	echo "build-wait: no $STATUS_FILE -- no build has run (or an old mac_compile.sh without the protocol)."
	exit 3
fi

while :; do
	read -r state second _rest < "$STATUS_FILE" || { sleep 1; continue; }
	case "$state" in
	running)
		if ! kill -0 "$second" 2>/dev/null; then
			# Give a just-finished build one beat to land its trap's final write, then re-check:
			# still "running" with a dead pid means the builder died without its EXIT trap.
			sleep 1
			read -r recheck _ < "$STATUS_FILE" || true
			if [[ "${recheck:-}" == "running" ]]; then
				echo "build-wait: builder (pid $second) vanished without writing a result -- treating as FAILED."
				exit 4
			fi
			continue
		fi
		sleep 3
		;;
	ok)
		echo "build-wait: build succeeded."
		exit 0
		;;
	failed)
		echo "build-wait: build FAILED (exit $second). Tail of the log is wherever you sent it."
		exit "$second"
		;;
	*)
		echo "build-wait: unrecognised status '$state' in $STATUS_FILE."
		exit 4
		;;
	esac
done
