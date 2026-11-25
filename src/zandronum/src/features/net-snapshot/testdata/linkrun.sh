#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] The linked-sector join test over linkmap.wad: a server, a player who makes the
# link and starts the descent, and a joiner that reads both floors part way down and again after
# the descent ends. Sent in index order the joiner's linked floor is out by the control's own step
# and stays out; in link order it matches the server.
#
#   bash src/zandronum/src/features/net-snapshot/testdata/linkrun.sh before
#   ENGINE=/path/to/other/forkundera bash .../linkrun.sh after
#
# What it needs:
#   * a dev build with the control bridge compiled in (cmake -DFUA_MCP_BRIDGE=ON, which
#     ZX_MCP_BRIDGE=1 sets); a release binary has no bridge and this script cannot drive it;
#   * ZANDRONUM_BRIDGE_PORT per instance, which this script sets itself -- the listener arms only
#     when it is present, binds 127.0.0.1, and registers the instance under ~/.forkundera;
#   * fuactl, run through node from tools/fuactl, which sends each console command and prints what
#     the instance printed;
#   * the first client to JOIN rather than spectate: a spectator triggers no line, so the link is
#     never made and the floor never moves. The script runs `join` for that reason.
#
# Reads the floors with fua_snap_sectorcheck, so nothing here depends on rendering: it runs on a
# machine with no GPU at a few frames a second.
set -uo pipefail

LABEL="${1:-run}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../../../../.." && pwd)"
ENGINE="${ENGINE:-$REPO/build-win/Release/forkundera.exe}"
IWAD="${IWAD:-$REPO/tools/freedoom/freedoom2.wad}"
MAP="$HERE/linkmap.wad"
LOGS="${LOGS:-${TMPDIR:-/tmp}/linkrun}"
FUACTL="node $REPO/tools/fuactl/src/cli.mjs"
PORT_SERVER=10811 PORT_A=10812 PORT_B=10813

mkdir -p "$LOGS"
say() { printf '\n=== %s\n' "$*"; }
read_both() {   # port, label
  $FUACTL console "fua_snap_sectorcheck 1" --port "$1" 2>&1 | grep -E "^\[|^sector" | sed "s/^/$2 /"
  $FUACTL console "fua_snap_sectorcheck 2" --port "$1" 2>&1 | grep -E "^sector" | sed "s/^/$2 /"
}
engine_at() {   # port, logname, extra args...
  local port="$1" name="$2"; shift 2
  ( ZANDRONUM_BRIDGE_PORT="$port" "$ENGINE" -iwad "$IWAD" -file "$MAP" -nosound -nomusic \
      -config "$LOGS/$LABEL-$name.ini" +logfile "$LOGS/$LABEL-$name" "$@" >/dev/null 2>&1 & )
}
stop_all() { taskkill //F //IM forkundera.exe >/dev/null 2>&1 || pkill -f forkundera >/dev/null 2>&1 || true; }

stop_all; sleep 1

say "server on MAP33"
engine_at $PORT_SERVER s -host +map MAP33 +sv_maxclients 4
sleep 9

say "client A connects, and joins rather than spectating"
engine_at $PORT_A a -connect 127.0.0.1
sleep 18
$FUACTL console "join" --port $PORT_A >/dev/null 2>&1
sleep 5

say "A walks the two triggers: the link, then the descent"
$FUACTL console "+forward" --port $PORT_A >/dev/null 2>&1
sleep 2
$FUACTL console "-forward" --port $PORT_A >/dev/null 2>&1
sleep 2
read_both $PORT_SERVER "server(moving)"

say "the joiner connects part way down"
engine_at $PORT_B b -connect 127.0.0.1
sleep 18

say "READING 1 -- the joiner, part way down"
read_both $PORT_B "joiner(mid)"
read_both $PORT_SERVER "server(mid)"

say "waiting out the descent"
sleep 75

say "READING 2 -- the joiner, after the descent ends"
read_both $PORT_B "joiner(end)"
read_both $PORT_SERVER "server(end)"
read_both $PORT_A "clientA(end)"

stop_all
echo "done: $LABEL (logs in $LOGS)"
