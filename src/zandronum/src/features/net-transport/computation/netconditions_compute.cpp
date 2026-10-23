// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-transport/computation/netconditions_compute.h"

namespace zx { namespace nettransport {

int ComputeFakeDelayMs(int pingMs, bool outbound)
{
	if (pingMs <= 0) return 0;
	return pingMs / 2 + (outbound ? pingMs % 2 : 0);
}

bool ComputeFakeDrop(unsigned &state, float lossPercent)
{
	if (!(lossPercent > 0.f)) return false;
	// [rc4l] Numerical Recipes' LCG, whose top 24 bits are spread evenly enough for a drop rate.
	state = state * 1664525u + 1013904223u;
	const float roll = (float)(state >> 8) / (float)(1u << 24) * 100.f;
	return roll < lossPercent;
}

void ComputeHoldPacket(HeldPackets &held, long long nowMs, int delayMs, const unsigned char *bytes, int length,
	const unsigned char *address, int addressLength)
{
	HeldPacket p;
	p.releaseMs = nowMs + (delayMs > 0 ? delayMs : 0);
	if (length > 0) p.bytes.assign(bytes, bytes + length);
	if (addressLength > 0) p.address.assign(address, address + addressLength);
	p.addressLength = addressLength > 0 ? addressLength : 0;
	held.queue.push_back(p);
}

bool ComputeAdmitDatagram(const HeldPackets &held, size_t capacity)
{
	return held.queue.size() < capacity;
}

bool ComputeTakeDuePacket(HeldPackets &held, long long nowMs, HeldPacket &out)
{
	if (held.queue.empty() || held.queue.front().releaseMs > nowMs) return false;
	out = held.queue.front();
	held.queue.pop_front();
	return true;
}

}} // namespace zx::nettransport
