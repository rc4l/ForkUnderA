#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# hosted-server-smoke.sh -- start a server the way the HOST tab starts one, and check it survives.
#
# WHY THIS EXISTS: nothing in CI ever started a server. The whole hosting path -- the thing a player
# actually uses -- was built and packaged but never run, and a crash that killed every hosted server
# within three seconds shipped anyway. It was a buffer overflow in the entry point's argv handling,
# so it needed no map, no client and no network to reproduce: only a REALISTIC NUMBER OF ARGUMENTS,
# which is the one thing a hand-typed command line never has.
#
# It does catch that one: run against the broken build on a CI runner this script segfaults in under
# a second. But it caught it there by LUCK OF LAYOUT -- the same binary on the machine where the bug
# was found survived this same test three times out of three, because an overflow is only fatal when
# whatever it lands on happens to matter. So this is a liveness check that happened to be enough
# once, and the ASan job is what actually sees the write. Do not retire that job on the strength of
# this one passing.
#
# So the argument list below is not a token gesture. It is the shape of what
# zx::BuildHostArgs emits for an ordinary co-op preset: a gameplay cvar block, a full map rotation
# and the server settings, comfortably past a hundred arguments. The count is asserted, because a
# smoke test that quietly shrank to ten arguments would pass forever while testing nothing.
#
# Cost: about a second. It reuses a binary the build job has already produced, so it adds no compile.
#
# Usage: tools/hosted-server-smoke.sh <engine-binary> [iwad]

set -uo pipefail

BIN="${1:-}"
IWAD="${2:-tools/freedoom/freedoom2.wad}"

# [rc4l] Long enough that a slow cold runner is not called a failure, short enough that a hang is
# still a failed job rather than a job that runs until the workflow timeout.
READY_TIMEOUT_SECS=90

# The crash this guards against killed the child one to three seconds in, AFTER it had printed its
# map and looked healthy. Reaching the map is therefore not the assertion -- outliving it is.
SURVIVE_SECS=8

PORT=27666

fail() { echo "::error::hosted-server-smoke: $*"; }

if [[ -z "$BIN" || ! -x "$BIN" ]]; then
	fail "no engine binary at '${BIN:-<unset>}'"
	exit 1
fi
if [[ ! -f "$IWAD" ]]; then
	fail "no IWAD at '$IWAD'"
	exit 1
fi

LOG="$(mktemp "${TMPDIR:-/tmp}/hosted-server-smoke.XXXXXX")"
trap 'rm -f "$LOG"' EXIT

# [rc4l] What the HOST tab hands a child, in the same order zx::BuildHostArgs writes it.
args=( -host -iwad "$IWAD" )

# The gameplay block: one argument pair per cvar the panel can set.
args+=( +skill 3 +cooperative true +survival false +invasion false +deathmatch false )
args+=( +teamplay false +duel false +terminator false +lastmanstanding false +teamlms false )
args+=( +possession false +teampossession false +teamgame false +ctf false +oneflagctf false )
args+=( +skulltag false +domination false )
args+=( +dmflags 2621444 +dmflags2 64 +zadmflags 268435524 +sv_forbidvoteflags 3066 )
args+=( +compatflags 0 +compatflags2 0 +zacompatflags 0 )
args+=( +lmsspectatorsettings 2 +lmsallowedweapons 1023 )

# The rotation: two arguments per map, and a full IWAD's worth of them is ordinary.
for i in $(seq -w 1 32); do
	args+=( +addmap "MAP$i" )
done

args+=( +map MAP01 -port "$PORT" )
args+=( +sv_hostname "smoke test server" +sv_maxclients 8 +sv_maxplayers 8 )
# Never announce a CI run to the public registry, and never advertise it on the runner's LAN.
args+=( +sv_fua_serverregistry_announce 0 +sv_broadcast 0 )
# The update check is an outbound request with nothing to say here.
args+=( +cl_fua_update_notify 0 )

# [rc4l] The bound that matters. The overflow needed more than sixty-four arguments to reach the
# statics past the array, so a list that no longer exceeds sixty-four has stopped testing for it.
if (( ${#args[@]} <= 64 )); then
	fail "argument list is only ${#args[@]} long; it must exceed 64 to exercise the case this guards"
	exit 1
fi
echo "hosted-server-smoke: starting $BIN with ${#args[@]} arguments on port $PORT"

"$BIN" "${args[@]}" > "$LOG" 2>&1 &
SERVER_PID=$!

report_and_die() {
	fail "$1"
	echo "--- server output (last 40 lines) ---"
	tail -40 "$LOG" || true
	kill -9 "$SERVER_PID" 2>/dev/null
	wait "$SERVER_PID" 2>/dev/null
	exit 1
}

# Up: either the hosting handshake, or the map banner every server prints.
deadline=$(( SECONDS + READY_TIMEOUT_SECS ))
until grep -qa -e "\[fua-host\] ready" -e "\*\*\* MAP01" "$LOG" 2>/dev/null; do
	if ! kill -0 "$SERVER_PID" 2>/dev/null; then
		report_and_die "the server exited before it reached its map"
	fi
	if (( SECONDS >= deadline )); then
		report_and_die "the server never reached its map within ${READY_TIMEOUT_SECS}s"
	fi
	sleep 0.2
done
echo "hosted-server-smoke: reached MAP01, now checking it stays up for ${SURVIVE_SECS}s"

# [rc4l] And STAYS up. The bug this exists for printed the map first and died afterwards.
for (( i = 0; i < SURVIVE_SECS; ++i )); do
	sleep 1
	if ! kill -0 "$SERVER_PID" 2>/dev/null; then
		report_and_die "the server died ${i}s after reaching its map"
	fi
done

kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
echo "hosted-server-smoke: OK -- ${#args[@]} arguments, reached MAP01, alive ${SURVIVE_SECS}s later"
