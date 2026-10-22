// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-transport/netconditions.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include <chrono>
#include "features/telemetry/telemetry.h"
#include <limits.h>

// [rc4l] Milliseconds added to this instance's round trip on the game's socket, for tests on loopback.
CVAR(Int, fua_net_fakeping, 0, 0)
// [rc4l] Percent of the packets this instance sends, and of those it receives, dropped, for tests on loopback.
CVAR(Float, fua_net_fakeloss, 0.f, 0)

namespace zx { namespace nettransport {

namespace {

HeldPackets g_outbound, g_inbound;
unsigned g_dropState = 0x2545F491u;

// [rc4l] A clock of its own: I_MSTime counts from a base set only once the timer is chosen, so a packet held at startup came due nine days later and every one behind it waited too.
long long ClockMs()
{
	return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool Simulating()
{
	return fua_net_fakeping > 0 || fua_net_fakeloss > 0.f;
}

// [rc4l] The clock packets are held against, or the end of time once the conditions are off so everything held goes.
long long Now()
{
	return Simulating() ? ClockMs() : LLONG_MAX;
}

bool Hold(HeldPackets &held, bool outbound, const unsigned char *bytes, int length, const void *address, int addressLength)
{
	if (ComputeFakeDrop(g_dropState, fua_net_fakeloss))
	{
		ZX_COUNT("net/fake_dropped", "packets", 1);
		return true;
	}
	ZX_COUNT("net/held", "packets", 1);
	ComputeHoldPacket(held, ClockMs(), ComputeFakeDelayMs(fua_net_fakeping, outbound), bytes, length,
		static_cast<const unsigned char *>(address), addressLength);
	return true;
}

}

bool ConditionsOn()
{
	return Simulating() || !g_outbound.queue.empty() || !g_inbound.queue.empty();
}

bool HoldOutbound(const unsigned char *bytes, int length, const void *address, int addressLength)
{
	// [rc4l] Still held ones go first, so a packet sent as the conditions switch off does not overtake them.
	if (!ConditionsOn()) return false;
	return Hold(g_outbound, true, bytes, length, address, addressLength);
}

bool TakeDueOutbound(HeldPacket &out)
{
	return ComputeTakeDuePacket(g_outbound, Now(), out);
}

void HoldInbound(const unsigned char *bytes, int length, const void *address, int addressLength)
{
	Hold(g_inbound, false, bytes, length, address, addressLength);
}

bool TakeDueInbound(HeldPacket &out)
{
	return ComputeTakeDuePacket(g_inbound, Now(), out);
}

}} // namespace zx::nettransport

// [rc4l] What the conditions hold now: each direction's packets and when the oldest is due.
CCMD(fua_net_conditions)
{
	const long long now = zx::nettransport::ClockMs();
	auto line = [now](const char *name, const zx::nettransport::HeldPackets &h) {
		if (h.queue.empty()) Printf("  %s: none held\n", name);
		else Printf("  %s: %d held, the oldest due in %lld ms\n", name, (int)h.queue.size(), h.queue.front().releaseMs - now);
	};
	Printf("fua_net_fakeping %d ms, fua_net_fakeloss %g%%\n", (int)fua_net_fakeping, (double)fua_net_fakeloss);
	line("out", zx::nettransport::g_outbound);
	line("in", zx::nettransport::g_inbound);
}
