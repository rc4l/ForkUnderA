// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Simulated network conditions for tests: packets held back by a fake ping and dropped at a fake loss rate, decided with no clock and no socket.

#ifndef ZX_NETCONDITIONS_COMPUTE_H
#define ZX_NETCONDITIONS_COMPUTE_H

#include <cstddef>
#include <deque>
#include <vector>

namespace zx { namespace nettransport {

// [rc4l] The one-way delay a packet takes in ms from a fake round trip of pingMs: half each way, the odd millisecond on the way out, none for a ping of 0 or less.
int ComputeFakeDelayMs(int pingMs, bool outbound);

// [rc4l] Whether the next packet is dropped at lossPercent (0 to 100), advancing the deterministic generator in state so a test that seeds it drops the same packets every run.
bool ComputeFakeDrop(unsigned &state, float lossPercent);

// [rc4l] One packet held back: when it may go, the bytes as they travel and the address they travel to or came from.
struct HeldPacket
{
	long long releaseMs = 0;
	std::vector<unsigned char> bytes;
	std::vector<unsigned char> address;   // [rc4l] the sockaddr_storage it goes to or came from, as raw bytes
	int addressLength = 0;
};

// [rc4l] Packets held back in the order they were held, which is the order they are let go: one delay per direction keeps them in order, as a real link with a steady ping does.
struct HeldPackets
{
	std::deque<HeldPacket> queue;
};

// [rc4l] Holds a packet to be let go at nowMs plus delayMs.
void ComputeHoldPacket(HeldPackets &held, long long nowMs, int delayMs, const unsigned char *bytes, int length,
	const unsigned char *address, int addressLength);

// [rc4l] The oldest held packet into out when its time has come at nowMs, true then and false while none is due.
bool ComputeTakeDuePacket(HeldPackets &held, long long nowMs, HeldPacket &out);

// [rc4l] Whether one more datagram joins a queue of at most capacity, which drops the newest rather than grow while the main thread is stalled.
bool ComputeAdmitDatagram(const HeldPackets &held, std::size_t capacity);

}} // namespace zx::nettransport

#endif // ZX_NETCONDITIONS_COMPUTE_H
