// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where a LAN announce is broadcast to, given the host's own IPv4 address.
//
// A directed subnet broadcast (A.255.255.255 and friends) is preferred over 255.255.255.255: the
// limited broadcast is filtered by some stacks and, on Linux, is not even permitted by the kernel in
// the general case. The network class decides how many leading octets are the network number and
// therefore how many trailing octets become 255 -- see the classful ranges below. Anything outside
// class A/B/C (0.x, 224.x multicast, 240.x reserved, and 127.x loopback which never wants a real
// broadcast) falls back to the limited broadcast, which is harmless if it goes nowhere.
//
// This is the one piece of the LAN-broadcast change with real edge cases (the class boundaries), so
// it is pulled out here to be unit-tested off-engine. Header-pure by the features/ rules: no engine
// types, no sockets.

#ifndef ZX_LANBROADCAST_COMPUTE_H
#define ZX_LANBROADCAST_COMPUTE_H

namespace zx
{

// Fills out[4] with the directed subnet-broadcast address for a host at ip[4]. For an address that
// is not in class A, B or C, out is set to the limited broadcast 255.255.255.255.
void ComputeSubnetBroadcast(const unsigned char ip[4], unsigned char out[4]);

} // namespace zx

#endif
