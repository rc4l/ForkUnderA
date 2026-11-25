#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Kaori Ishida
#
# [Kaori-Chani] The join handshake over a server and one client: a connect, a map change and a
# reconnect with the gate off, where not one snapshot command may go out; then the gate on, which
# waits for the next map; then off again, which must take effect at once. A second server starts
# with the gate already on, to prove the latch runs at the first map load too.
#
#   bash src/zandronum/src/features/net-snapshot/testdata/joinrun.sh
#
# What it needs:
#   * a dev build with the control bridge compiled in (cmake -DFUA_MCP_BRIDGE=ON);
#   * ZANDRONUM_BRIDGE_PORT per instance, which this script sets itself;
#   * fuactl, run through node from tools/fuactl.
#
# How the wire is read: fua_netcap on the server records every command sent to a client, as
# "tic client r|u id extended length hex", so the extended ids below are counted from it. Note
# cl_showcommands cannot serve here -- it is CVAR_DEBUGONLY, so it does nothing in a release
# build. The snapshot datagrams do not pass through NetCommand, so they are read instead from the
# server's own [snap] stats and from its per-client ack, which only moves if a client applied a
# datagram, which in turn needs the SnapJoin anchor.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../../../../.." && pwd)"
ENGINE="${ENGINE:-$REPO/build-win/Release/forkundera.exe}"
IWAD="${IWAD:-$REPO/tools/freedoom/freedoom2.wad}"
LOGS="${LOGS:-${TMPDIR:-/tmp}/joinrun}"
FUACTL="node $REPO/tools/fuactl/src/cli.mjs"
PORT_S=10861 PORT_C=10862

# The engine is a Windows binary, so a path it must open itself (fua_netcap's file) is handed to
# it in the form Windows reads, not the shell's: an MSYS path simply fails to open.
winpath() { command -v cygpath >/dev/null && cygpath -m "$1" || printf '%s' "$1"; }

# SVC2 wire ids, as tools/data/protocol-snapshot.txt lists them.
ID_SNAPSHOT=81 ID_SNAPJOIN=83 ID_MOVERGATE=85 ID_SOUNDTABLE=86

rm -rf "$LOGS"; mkdir -p "$LOGS"
say() { printf '\n=== %s\n' "$*"; }
sv() { $FUACTL console "$1" --port $PORT_S 2>&1 | tail -1; }
cl() { $FUACTL console "$1" --port $PORT_C 2>&1 | tail -1; }
slog() { cat "$LOGS"/$1.txt*.log 2>/dev/null; }

# How many commands of one extended id the capture holds.
capcount() { awk -v want="$2" '$5 == want' "$1" 2>/dev/null | wc -l | tr -d ' '; }
capreport() {
  printf '%-26s SNAPJOIN %s  SOUNDTABLE %s  MOVERGATE %s  (captured %s commands)\n' \
    "$2" "$(capcount "$1" $ID_SNAPJOIN)" "$(capcount "$1" $ID_SOUNDTABLE)" \
    "$(capcount "$1" $ID_MOVERGATE)" "$(wc -l < "$1" 2>/dev/null | tr -d ' ')"
}

serve() {   # logname, extra args...
  local name="$1"; shift
  ( ZANDRONUM_BRIDGE_PORT=$PORT_S "$ENGINE" -iwad "$IWAD" -host +map MAP01 +sv_maxclients 4 \
      -nosound -nomusic -config "$LOGS/$name.ini" +logfile "$LOGS/$name.txt" "$@" \
      >/dev/null 2>&1 & )
}
connect() {  # logname
  ( ZANDRONUM_BRIDGE_PORT=$PORT_C "$ENGINE" -iwad "$IWAD" -connect 127.0.0.1 \
      -nosound -nomusic -config "$LOGS/$1.ini" +logfile "$LOGS/$1.txt" >/dev/null 2>&1 & )
}
stop_all() { taskkill //F //IM forkundera.exe >/dev/null 2>&1 || pkill -f forkundera >/dev/null 2>&1 || true; }

stop_all; sleep 1

# ---------------------------------------------------------------- the gate off
say "PHASE 1: a server on MAP01 with the gate off, recording every command it sends"
serve server1
sleep 10
sv "fua_netcap_start $(winpath "$LOGS/off.netcap")"
sv "fua_snap_stats 1" >/dev/null

say "a client connects and joins"
connect client1
sleep 20
cl "join" >/dev/null
sleep 6

say "the server changes map"
sv "map MAP02" >/dev/null
sleep 22

say "and the client reconnects"
cl "disconnect" >/dev/null
sleep 4
cl "connect 127.0.0.1" >/dev/null
sleep 20
cl "join" >/dev/null
sleep 6

say "fua_snap 1 mid-map: it must say it waits, and still send nothing"
sv "fua_snap_send 1"; sv "fua_snap_nolegacy 0"; sv "fua_snap 1"
sleep 10
sv "fua_netcap_stop"
capreport "$LOGS/off.netcap" "gate off, all three"
printf 'the server said it waits: %s (want 1)\n' "$(slog server1 | grep -c 'takes effect at the next map')"
printf 'and printed %s [snap] stat lines (want 0)\n' "$(slog server1 | grep -c '\[snap\]')"

# ----------------------------------------------------------------- the gate on
say "PHASE 2: the next map, where the gate is latched on"
sv "fua_netcap_start $(winpath "$LOGS/on.netcap")"
sv "map MAP03" >/dev/null
sleep 22
# A map change leaves the client spectating, and a spectator is sent no local block, so the
# no-sink path below would never be reached without this.
cl "join" >/dev/null
sleep 10
capreport "$LOGS/on.netcap" "gate on, next map"
printf 'local blocks, with no sink installed on the client:
'
slog server1 | grep "local block" | tail -1
printf 'the server now reports: %s [snap] lines\n' "$(slog server1 | grep -c '\[snap\]')"
slog server1 | grep -E "ack=" | tail -2

say "fua_snap 0 mid-map, which must take effect at once"
sv "fua_snap 0"
sleep 4
sv "fua_netcap_stop"
printf 'a MOVERGATE on the way out: %s\n' "$(capcount "$LOGS/on.netcap" $ID_MOVERGATE)"
sleep 12
printf 'the last [snap] line after the off:\n'; slog server1 | grep '\[snap\]' | tail -1

# ------------------------------------------------- the latch at the first map
say "PHASE 3: a second server, started with the gate already on"
stop_all; sleep 2
serve server2 +fua_snap 1 +fua_snap_send 1 +fua_snap_nolegacy 0
sleep 10
sv "fua_netcap_start $(winpath "$LOGS/first.netcap")"
sv "fua_snap_stats 1" >/dev/null
connect client2
sleep 20
cl "join" >/dev/null
sleep 10
sv "fua_netcap_stop"
capreport "$LOGS/first.netcap" "gate on from MAP01"
printf 'the server said it waits: %s (want 0)\n' "$(slog server2 | grep -c 'takes effect at the next map')"
slog server2 | grep -E "ack=|outbound" | tail -3

stop_all
echo
echo "done (logs and captures in $LOGS)"
