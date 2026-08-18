#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Prove that one server listed under two addresses shows up as ONE row.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$(cd ../.. && pwd)"

KEEP=0
[ "${1:-}" = "--keep" ] && KEEP=1

say()  { printf '\033[32m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[31mDUALSTACK FAILED: %s\033[0m\n' "$*" >&2; dump; exit 1; }

COMPOSE="docker compose -f docker-compose.dualstack.yml -p natlab-dual"
dc() { $COMPOSE "$@"; }

dump() {
    echo "--- registry: what it holds ---"
    dc logs registry 2>&1 | grep -iE "adding|collision" | tail -12 || true
    echo "--- host: announces ---"
    dc exec -T host sh -lc 'grep -iE "announce|registry" /tmp/engine.log | tail -12' 2>&1 || true
    echo "--- client: rows ---"
    dc exec -T client node /fuactl/src/cli.mjs rpc browser.list --port 27800 --token natlab 2>&1 | tail -30 || true
}

cleanup() { [ "$KEEP" = "1" ] || dc down -v --remove-orphans >/dev/null 2>&1 || true; }
trap cleanup EXIT

say "Staging engine + fuactl..."
ENGINE_SRC="${NATLAB_ENGINE_DIR:-$ROOT/build-linux}"
[ -x "$ENGINE_SRC/forkundera" ] || fail "no engine at $ENGINE_SRC/forkundera"

rm -rf engine fuactl && mkdir -p engine
cp "$ENGINE_SRC/forkundera" engine/
cp "$ENGINE_SRC"/*.pk3 engine/ 2>/dev/null || fail "no pk3s beside the engine binary"
IWAD="${NATLAB_IWAD:-$ROOT/build-linux/freedoom2.wad}"
[ -f "$IWAD" ] || IWAD="$(find "$ROOT" -maxdepth 3 -name 'freedoom2.wad' -print -quit 2>/dev/null || true)"
[ -f "$IWAD" ] || fail "no IWAD found; set NATLAB_IWAD"
cp "$IWAD" engine/iwad.wad
cp -R "$ROOT/tools/fuactl" fuactl

say "Starting..."
dc build --quiet
dc up -d --force-recreate

# The fixture is only meaningful if the registry really is reachable both ways. A name that silently
# resolves to one family would make this whole run a very slow way of testing IPv4.
say "Verifying the fixture: the registry answers on BOTH families..."
dc exec -T host sh -lc 'ping -c1 -W2 203.0.113.10 >/dev/null 2>&1' \
    || fail "host cannot reach the registry over IPv4"
dc exec -T host sh -lc 'ping6 -c1 -W2 fd00:cafe:1::10 >/dev/null 2>&1 || ping -6 -c1 -W2 fd00:cafe:1::10 >/dev/null 2>&1' \
    || fail "host cannot reach the registry over IPv6 -- the fixture is single-stack and proves nothing"

start_engine() { # $1=peer  $2=extra args
    dc exec -d -T "$1" sh -lc "
        Xvfb :99 -screen 0 640x480x24 >/tmp/xvfb.log 2>&1 &
        sleep 2
        DISPLAY=:99 ZANDRONUM_BRIDGE_PORT=27800 ZANDRONUM_BRIDGE_TOKEN=natlab \
        /engine/forkundera -iwad /engine/iwad.wad -nosound +map MAP01 \
            +set developer 1 \
            +set fua_serverregistry_host registry.natlab \
            +set cl_fua_serverregistry_list registry.natlab \
            +set cl_fua_serverregistrylist_fetch 0 \
            $2 >/tmp/engine.log 2>&1 &
    "
}

say "Starting the host engine..."
start_engine host "-host +set sv_hostname DUALSTACK-HOST +set sv_fua_serverregistry_announce 1 +set sv_fua_serverregistry_enforcebans 1"
say "Starting the client engine..."
start_engine client ""

# [rc4l] Under a timeout, because a container whose networking is broken makes exec HANG rather than
# fail, and a hang inside a retry loop consumes the whole job and reports as a timeout.
fua() { local peer="$1"; shift; timeout 60 dc exec -T "$peer" node /fuactl/src/cli.mjs "$@" --port 27800 --token natlab; }

say "Waiting for both bridges..."
for peer in host client; do
    ok=0
    for _ in $(seq 1 45); do
        if fua "$peer" rpc sim.tic >/dev/null 2>&1; then ok=1; break; fi
        sleep 2
    done
    [ "$ok" = "1" ] || fail "$peer engine never opened its bridge"
done

# The announce runs on a 30s cycle, and the second one only happens if the AAAA resolved.
say "[1/4] the registry holds this server under BOTH families..."
both=0
for _ in $(seq 1 25); do
    v4=$( dc logs registry 2>&1 | grep -c "Adding 203.0.113.20" || true )
    v6=$( dc logs registry 2>&1 | grep -c "Adding \[fd00:cafe:1::20\]" || true )
    if [ "$v4" -ge 1 ] && [ "$v6" -ge 1 ]; then both=1; break; fi
    sleep 3
done
[ "$both" = "1" ] || fail "the registry did not receive one announce per family -- the IPv6 announce is not arriving"

say "[2/4] the registry did not call it a collision..."
if dc logs registry 2>&1 | grep -qi "Registry id collision"; then
    fail "one server's two announces were treated as a collision, so they will never be grouped"
fi

say "[3/4] the client shows ONE row for it..."
ok=0
for _ in $(seq 1 20); do
    rows="$( fua client browser --wait 12 2>/dev/null || true )"
    held=$( echo "$rows" | grep -c '"name": "DUALSTACK-HOST"' || true )
    shown=$( echo "$rows" | python3 -c "
import json,sys
try: d=json.load(sys.stdin)
except Exception: print(0); raise SystemExit
print(sum(1 for s in d.get('servers',[]) if s.get('name')=='DUALSTACK-HOST' and s.get('listed')))" 2>/dev/null || echo 0 )

    if [ "$held" -ge 2 ] && [ "$shown" = "1" ]; then ok=1; break; fi
    sleep 3
done

if [ "$ok" != "1" ]; then
    echo "held=$held shown=$shown" >&2
    [ "${held:-0}" -lt 2 ] && fail "the client never held both addresses, so there was nothing to collapse"
    fail "the client holds both addresses but shows $shown rows -- dedupe did not collapse them"
fi

say "PASS: one server, listed twice by the registry, shown once to the player."

# [rc4l] And the case that has no unit test anywhere: a player with no IPv4 at all.
#
# The client resolved its registry with gethostbyname/AF_INET, so it asked for an A record and
# nothing else -- a v6-only player reached no registry and saw an empty browser with no error. That
# is invisible on any dual-stack machine, which is every machine we own, so it can only be caught
# here by taking the IPv4 address away.
# [rc4l] The client that never had an IPv4 address at all, which is the only honest way to test a
# v6-only player: a dual-stack machine can always fall back, so nothing short of the address being
# absent from the start proves anything.
say "[4/4] a client with NO IPv4 still finds the server..."

timeout 30 dc exec -T client6 sh -lc 'ip -o -4 addr show | grep -q "inet 1\|inet 2" && exit 1; exit 0' \
    || fail "client6 has an IPv4 address, so this case would prove nothing"

start_engine client6 ""

ok6=0
for _ in $(seq 1 45); do
    if fua client6 rpc sim.tic >/dev/null 2>&1; then ok6=1; break; fi
    sleep 2
done
[ "$ok6" = "1" ] || fail "the v6-only client never opened its bridge"

found6=0
for _ in $(seq 1 20); do
    if fua client6 browser --wait 12 2>/dev/null | grep -q 'DUALSTACK-HOST'; then found6=1; break; fi
    sleep 3
done
[ "$found6" = "1" ] || fail "a client with no IPv4 could not reach the registry, so it saw nothing"

say "PASS: a client with no IPv4 reached the registry and found the server."
