// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Test-only network conditions on the game's socket: fua_net_fakeping adds to this instance's round trip, half on each packet it sends and half on each it receives, and fua_net_fakeloss drops that share of packets each way.

#ifndef ZX_NETCONDITIONS_H
#define ZX_NETCONDITIONS_H

#include "features/net-transport/computation/netconditions_compute.h"

namespace zx { namespace nettransport {

// [rc4l] Whether a fake ping or loss is set, or packets are still held from one, so the game's socket goes through the calls below.
bool ConditionsOn();

// [rc4l] An encoded packet on its way out taken over, held for the fake ping or dropped; false when there are no conditions and the caller sends it itself.
bool HoldOutbound(const unsigned char *bytes, int length, const void *address, int addressLength);

// [rc4l] The oldest held outgoing packet whose time has come, into out; held packets all come due once the conditions are switched off.
bool TakeDueOutbound(HeldPacket &out);

// [rc4l] A packet just received held for the fake ping, or dropped.
void HoldInbound(const unsigned char *bytes, int length, const void *address, int addressLength);

// [rc4l] The oldest held received packet whose time has come, into out.
bool TakeDueInbound(HeldPacket &out);

}} // namespace zx::nettransport

#endif // ZX_NETCONDITIONS_H
