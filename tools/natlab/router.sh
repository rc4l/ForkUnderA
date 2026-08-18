#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l

# [rc4l] One container pretending to be a home router.

set -eu

FLAVOUR="${NAT_FLAVOUR:-portrestricted}"

# [rc4l] Find the WAN side by its ADDRESS, never by its name.
WAN_PREFIX="${NAT_WAN_PREFIX:?NAT_WAN_PREFIX must name the public subnet, e.g. 203.0.113.}"
WAN_IF="$( ip -o -4 addr show | awk -v p="$WAN_PREFIX" '$4 ~ ("^" p) { print $2; exit }' )"

if [ -z "$WAN_IF" ]; then
    echo "router: no interface holds an address in $WAN_PREFIX -- this box cannot reach the internet side" >&2
    ip -o -4 addr show >&2
    exit 1
fi

echo "router: flavour=$FLAVOUR wan=$WAN_IF (prefix $WAN_PREFIX)"
ip -o -4 addr show

# Forwarding is off by default in the container's netns; without this the box is a wall, not a router.
# compose sets it too, and whichever gets there first is fine -- but a read-only /proc/sys must not
# take the container down, so this is best-effort and the check below is what actually decides.
sysctl -w net.ipv4.ip_forward=1 >/dev/null 2>&1 || true
[ "$(cat /proc/sys/net/ipv4/ip_forward 2>/dev/null || echo 0)" = "1" ] \
    || { echo "router: ip_forward is off and could not be set -- this box would silently be a wall" >&2; exit 1; }

# [rc4l] Internet-like latency, and it is not decoration.
tc qdisc add dev "$WAN_IF" root netem delay "${NAT_LATENCY_MS:-25}ms" 2>/dev/null \
    || echo "router: WARNING could not add latency; the punch race will be unrealistically tight" >&2

case "$FLAVOUR" in
    fullcone)
        # [rc4l] The permissive router: an endpoint-INDEPENDENT mapping, where anything arriving at
        # the mapped port is delivered inside regardless of who sent it.
        iptables -t nat -A POSTROUTING -o "$WAN_IF" -j MASQUERADE
        iptables -t nat -A PREROUTING -i "$WAN_IF" -p udp --dport "${NAT_GAME_PORT:-10666}" \
            -j DNAT --to-destination "${NAT_LAN_PEER:?fullcone needs NAT_LAN_PEER}:${NAT_GAME_PORT:-10666}"
        ;;
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

# [rc4l] Inbound is dropped unless it belongs to a flow we already saw.
if [ "$FLAVOUR" = "fullcone" ]; then
    iptables -A FORWARD -i "$WAN_IF" -p udp --dport "${NAT_GAME_PORT:-10666}" -j ACCEPT
fi

iptables -A FORWARD -i "$WAN_IF" -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i "$WAN_IF" -j DROP

echo "router: ready"
# Nothing else to do; the kernel does the work. Sleep as PID 1 so compose keeps the netns alive.
exec sleep infinity
