#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] Prove that two players behind two different routers can find each other and connect.
#
# WHAT THIS REPLACES. The only honest way to test this used to be two machines on two real networks
# -- in practice a laptop on a phone hotspot, run by hand, once, by someone who remembered. That is
# not a gate, it is a ritual, and it cannot run per release. Everything here exists so the answer
# arrives from CI instead.
#
# WHAT MAKES IT A REAL TEST. Neither peer can reach the other. They are on `internal: true` networks
# behind masquerading routers that drop unsolicited inbound (see router.sh). So a connection that
# succeeds could only have been carried by the mechanism under test. The most valuable line in this
# whole directory is the DROP rule; if it ever stops applying, every assertion below keeps passing
# and stops meaning anything, which is why assert_nat_is_real runs FIRST and fails the run if the
# fixture has gone soft.
#
# Usage:  ./run.sh [--host-nat portrestricted|symmetric] [--client-nat ...] [--keep]

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$(cd ../.. && pwd)"

HOST_NAT="portrestricted"
CLIENT_NAT="portrestricted"
KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --host-nat)   HOST_NAT="$2"; shift 2 ;;
        --client-nat) CLIENT_NAT="$2"; shift 2 ;;
        --keep)       KEEP=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
export NAT_FLAVOUR_HOST="$HOST_NAT" NAT_FLAVOUR_CLIENT="$CLIENT_NAT"

# Symmetric NAT cannot be punched -- the mapping the registry saw is not the mapping the host is told
# to aim at. That is a property of the internet, not a bug in us, so the lab asserts the DEGRADATION
# is clean (refused fast, ordinary connection still attempted) rather than asserting success.
EXPECT_PUNCH=1
[ "$HOST_NAT" = "symmetric" ] && EXPECT_PUNCH=0
[ "$CLIENT_NAT" = "symmetric" ] && EXPECT_PUNCH=0

say()  { printf '\033[32m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[31mNATLAB FAILED: %s\033[0m\n' "$*" >&2; dump_diagnostics; exit 1; }

COMPOSE="docker compose"
dc() { $COMPOSE "$@"; }

dump_diagnostics() {
    # [rc4l] The punch lines are the point, and a plain tail buries them: the registry prints one
    # "Sending server list" per query, which is several per second, so forty lines of tail is forty
    # lines of the least interesting thing it does.
    echo "--- registry: punch decisions ---"
    dc logs registry 2>&1 | grep -i "punch" | tail -20 || echo "(the registry logged no punch activity at all)"
    echo "--- registry: what it holds ---"
    dc logs registry 2>&1 | grep -iE "adding|verif" | tail -10 || true

    echo "--- client: what its browser holds (after a fresh refresh) ---"
    # Refresh first. Rows expire, and by the time a failing assertion gives up its retries the list
    # has usually emptied -- which reads as "the client never knew about the server" when in fact it
    # knew, asked, and timed out.
    dc exec -T client node /fuactl/src/cli.mjs rpc browser.refresh --port 27800 --token natlab >/dev/null 2>&1 || true
    sleep 6
    dc exec -T client node /fuactl/src/cli.mjs rpc browser.list --port 27800 --token natlab 2>&1 | tail -40 || true

    echo "--- client engine log (punch) ---"
    dc exec -T client sh -lc 'grep -iE "punch|registry" /tmp/engine.log | tail -20' 2>&1 || true
    echo "--- host engine log (punch) ---"
    dc exec -T host sh -lc 'grep -iE "punch|registry" /tmp/engine.log | tail -20' 2>&1 || true

    echo "--- router_host log ---"; dc logs --tail 20 router_host 2>&1 || true
    echo "--- host engine log (tail) ---"; dc exec -T host   sh -lc 'tail -30 /tmp/engine.log' 2>&1 || true
    echo "--- client engine log (tail) ---"; dc exec -T client sh -lc 'tail -30 /tmp/engine.log' 2>&1 || true
}

cleanup() { [ "$KEEP" = "1" ] || dc down -v --remove-orphans >/dev/null 2>&1 || true; }
trap cleanup EXIT

# --- 1. Stage what the peer image copies in -----------------------------------------------------
# The engine is built OUTSIDE this script (CI builds it once, natively, with ZX_MCP_BRIDGE=1). Taking
# a prebuilt tree keeps the lab about networking instead of about compiling.
say "Staging engine + fuactl into the image context..."
ENGINE_SRC="${NATLAB_ENGINE_DIR:-$ROOT/build-linux}"
[ -x "$ENGINE_SRC/forkundera" ] || fail "no engine at $ENGINE_SRC/forkundera -- build it with ZX_MCP_BRIDGE=1 ./linux_compile.sh --no-package"

rm -rf engine fuactl && mkdir -p engine
cp "$ENGINE_SRC/forkundera" engine/
cp "$ENGINE_SRC"/*.pk3 engine/ 2>/dev/null || fail "no pk3s next to the engine binary -- a build that cannot start is not a network test"
# An IWAD, or the engine stops at the picker and every timeout below is a lie about the network.
IWAD="${NATLAB_IWAD:-$ROOT/build-linux/freedoom2.wad}"
[ -f "$IWAD" ] || IWAD="$(find "$ROOT" -maxdepth 3 -name 'freedoom2.wad' -print -quit 2>/dev/null || true)"
[ -f "$IWAD" ] || fail "no IWAD found; set NATLAB_IWAD"
cp "$IWAD" engine/iwad.wad
cp -R "$ROOT/tools/fuactl" fuactl

# --- 2. Bring the lab up ------------------------------------------------------------------------
say "Building images (host NAT=$HOST_NAT, client NAT=$CLIENT_NAT)..."
dc build --quiet
say "Starting..."
dc up -d --force-recreate

# Peers are born with no route off their LAN; point them at their router.
for peer in host client; do
    gw="$(dc exec -T "$peer" sh -lc 'echo $PEER_GATEWAY' | tr -d '\r\n')"
    dc exec -T "$peer" sh -lc "ip route replace default via $gw" || fail "$peer: could not route via $gw"
done

# --- 3. The fixture must actually isolate them ---------------------------------------------------
# Runs BEFORE anything is asserted about the engine. If the peers can already reach each other, every
# later assertion is vacuous, and a vacuously passing network test is worse than no test: it is a
# green check that says the punch works when the punch never ran.
say "Verifying the fixture: the peers must NOT be able to reach each other..."
if dc exec -T client sh -lc 'ping -c1 -W2 192.168.241.20 >/dev/null 2>&1'; then
    fail "client can ping the host directly -- the NAT isolation is not in effect, so this lab proves nothing"
fi
dc exec -T client sh -lc 'ping -c1 -W2 203.0.113.10 >/dev/null 2>&1' \
    || fail "client cannot reach the registry -- the lab is broken in the other direction"
say "Fixture OK: peers isolated, registry reachable from both."

# --- 4. Start both engines ----------------------------------------------------------------------
# Xvfb because the client half initialises GL before reaching its main loop; nothing here looks at a
# frame. -nosound keeps the sim's RNG stream clean and drops an entire class of container audio
# failures that have nothing to do with networking.
REG=203.0.113.10
start_engine() { # $1=peer  $2=extra args
    dc exec -d -T "$1" sh -lc "
        Xvfb :99 -screen 0 640x480x24 >/tmp/xvfb.log 2>&1 &
        sleep 2
        DISPLAY=:99 ZANDRONUM_BRIDGE_PORT=27800 ZANDRONUM_BRIDGE_TOKEN=natlab \
        /engine/forkundera -iwad /engine/iwad.wad -nosound +map MAP01 \
            +set developer 1 \
            +set fua_serverregistry_host $REG \
            +set cl_fua_serverregistry_list $REG \
            +set cl_fua_serverregistrylist_fetch 0 \
            $2 >/tmp/engine.log 2>&1 &
    "
}

say "Starting the host engine..."
start_engine host "-host +set sv_hostname NATLAB-HOST +set sv_fua_serverregistry_announce 1"
say "Starting the client engine..."
start_engine client ""

fua() { # $1=peer, rest = fuactl args
    local peer="$1"; shift
    dc exec -T "$peer" node /fuactl/src/cli.mjs "$@" --port 27800 --token natlab
}

say "Waiting for both bridges..."
for peer in host client; do
    ok=0
    for _ in $(seq 1 45); do
        if fua "$peer" rpc sim.tic >/dev/null 2>&1; then ok=1; break; fi
        sleep 2
    done
    [ "$ok" = "1" ] || fail "$peer engine never opened its bridge (see the engine log below)"
done
say "Both engines up."

# --- 5. Assertions ------------------------------------------------------------------------------

# (a) The host announces THROUGH its NAT and the registry lists it. Outbound always works, so this
#     is the easy half -- but if it fails, nothing after it can be interpreted.
say "[1/4] host reaches the registry and is listed..."
listed=0
for _ in $(seq 1 20); do
    if fua host rpc net.hostdiag | grep -q '"state": *"listed_'; then listed=1; break; fi
    sleep 3
done
[ "$listed" = "1" ] || fail "the host never became listed -- its announce is not reaching the registry"

# (b) ...and must NOT claim to be reachable, because nothing here can prove that.
#
# This assertion has already earned its place. It used to fail, and the diagnostic was wrong rather
# than the lab: the registry's verification reply travels back through the NAT mapping the announce
# opened, so it arrives however closed the port is to everyone else, and reporting that as "reachable"
# told players behind an unforwarded router that strangers could join them.
say "[2/4] host does not claim reachability it cannot prove..."
if fua host rpc net.hostdiag | grep -q '"reachable": *true'; then
    fail "the host claims reachable from behind a NAT with no forward -- nothing available to it can support that claim"
fi

# (c) The client finds the server through the registry, not by LAN broadcast (they share no LAN).
say "[3/4] client discovers the server through the registry..."
found=0
target=""
for _ in $(seq 1 20); do
    listing="$( fua client browser --wait 12 2>/dev/null || true )"
    if echo "$listing" | grep -q 'NATLAB-HOST'; then
        # [rc4l] Take the address the BROWSER holds rather than naming one here. Behind masquerade the
        # host's port is whatever its router chose, and the registry recorded the address the announce
        # arrived from -- so a hardcoded "router:10666" is a guess that happens to be right only while
        # the NAT preserves the port. It is also what a player does: they click the row.
        target="$( echo "$listing" | grep -A 2 'NATLAB-HOST' | grep -oE '"address": *"[^"]+"' | head -1 | sed 's/.*"address": *"//; s/"$//' )"
        found=1
        break
    fi
    sleep 3
done
if [ "$found" != "1" ]; then
    # [rc4l] Under symmetric NAT this is the CORRECT outcome, not a failure. The server's row never
    # answers because the punch cannot land, so the name never arrives. Demanding it here would be
    # demanding that the internet work differently.
    if [ "$EXPECT_PUNCH" = "0" ]; then
        say "expected: symmetric NAT means the server's row never answers, so it stays nameless."
        fua client rpc sim.tic >/dev/null 2>&1 \
            || fail "the client is wedged after a punch that could not land -- a failed punch must never take the game with it"
        say "PASS (expected): punch defeated by symmetric NAT, client still responsive."
        exit 0
    fi
    fail "the client never saw the server in the registry-backed list"
fi
[ -n "$target" ] || fail "found the server in the list but could not read its address back"
say "    the registry holds it at $target"

# (d) The proof. Connect, and assert from the SERVER that somebody is actually in -- the only signal
#     that cannot be produced by a connection which did not happen.
say "[4/4] client connects to $target, and the server confirms it..."
fua client ui exec "connect $target" >/dev/null 2>&1 || true
connected=0
for _ in $(seq 1 25); do
    if fua host rpc net.clients | grep -qE '"connected": *[1-9]'; then connected=1; break; fi
    sleep 2
done

if [ "$EXPECT_PUNCH" = "1" ]; then
    [ "$connected" = "1" ] || fail "no client connected: two peers behind port-restricted NATs could not reach each other, which is exactly what the punch is for"
    say "PASS: a client behind its own NAT connected to a server behind another one."
else
    # Symmetric: the punch cannot work. What must hold is that we degrade cleanly rather than hang.
    if [ "$connected" = "1" ]; then
        say "PASS (better than required): the connection succeeded even with a symmetric NAT."
    else
        say "PASS (expected): symmetric NAT defeated the punch, and the attempt ended rather than hanging."
        fua client rpc sim.tic >/dev/null 2>&1 || fail "the client is wedged after a failed punch -- a refused punch must never take the game with it"
    fi
fi

say "NAT lab complete (host=$HOST_NAT client=$CLIENT_NAT)."
