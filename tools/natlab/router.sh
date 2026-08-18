#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] One container pretending to be a home router.
#
# The whole point of the lab is that the peers are NOT reachable. A test where both ends can already
# talk to each other proves nothing about hole punching, because the punch is never what carried the
# packet -- and that is the easiest test in the world to write by accident. So each peer sits on its
# own LAN with this box as its only way out, doing what a consumer router does: rewrite the source of
# outbound packets, remember the flow, and drop anything inbound that does not match one it saw.
#
# NAT_FLAVOUR picks how the mapping behaves, because "does punching work" has different answers:
#
#   portrestricted  Linux MASQUERADE's own behaviour: one external port per internal source port,
#                   inbound accepted only from an address:port already spoken to. The common home
#                   router, and the case punching is designed for.
#   symmetric       A fresh external port per DESTINATION. The joiner's port is then not the port the
#                   registry saw, so the address the host is told to punch is wrong before it is sent.
#                   Punching is EXPECTED to fail here; the lab asserts the failure is clean and the
#                   ordinary connection still happens, which is the promise the punch broker makes.
#
# Emulated with --random-fully rather than a kernel patch: per-destination port randomisation is what
# makes a mapping symmetric from the far end's point of view, which is the property under test.

set -eu

FLAVOUR="${NAT_FLAVOUR:-portrestricted}"
WAN_IF="${NAT_WAN_IF:-eth0}"

echo "router: flavour=$FLAVOUR wan=$WAN_IF"

# Forwarding is off by default in the container's netns; without this the box is a wall, not a router.
# compose sets it too, and whichever gets there first is fine -- but a read-only /proc/sys must not
# take the container down, so this is best-effort and the check below is what actually decides.
sysctl -w net.ipv4.ip_forward=1 >/dev/null 2>&1 || true
[ "$(cat /proc/sys/net/ipv4/ip_forward 2>/dev/null || echo 0)" = "1" ] \
    || { echo "router: ip_forward is off and could not be set -- this box would silently be a wall" >&2; exit 1; }

case "$FLAVOUR" in
    symmetric)
        # --random-fully allocates a fresh source port per flow rather than preserving the internal
        # one, so two flows from the same internal port to different destinations get different
        # external ports. That is the definition of symmetric, and the reason punching cannot work.
        iptables -t nat -A POSTROUTING -o "$WAN_IF" -j MASQUERADE --random-fully
        ;;
    portrestricted)
        iptables -t nat -A POSTROUTING -o "$WAN_IF" -j MASQUERADE
        ;;
    *)
        echo "router: unknown NAT_FLAVOUR '$FLAVOUR'" >&2
        exit 2
        ;;
esac

# [rc4l] Inbound is dropped unless it belongs to a flow we already saw. This line IS the test
# fixture: delete it and every assertion below still "passes" while proving nothing, because the
# packets would simply be routed in. A punch works by making the joiner's traffic ESTABLISHED from
# this table's point of view, so RELATED,ESTABLISHED is exactly the door it opens.
iptables -A FORWARD -i "$WAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i "$WAN_IF" -j DROP

echo "router: ready"
# Nothing else to do; the kernel does the work. Sleep as PID 1 so compose keeps the netns alive.
exec sleep infinity
