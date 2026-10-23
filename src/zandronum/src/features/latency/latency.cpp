// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/latency/latency.h"
#include "features/latency/computation/latency_compute.h"
#include "features/telemetry/telemetry.h"

namespace zx { namespace latency {

namespace {

LatencyStamp g_input, g_packet;

const zx::telemetry::Counter &InputCounter()
{
	static const zx::telemetry::Counter c = zx::telemetry::Register(zx::telemetry::CounterSpec{ "latency/input_to_frame_ms", "ms", zx::telemetry::CounterKind::Time, 0 });
	return c;
}

const zx::telemetry::Counter &PacketCounter()
{
	static const zx::telemetry::Counter c = zx::telemetry::Register(zx::telemetry::CounterSpec{ "latency/packet_to_frame_ms", "ms", zx::telemetry::CounterKind::Time, 0 });
	return c;
}

}

void NoteInput()
{
	ComputeStampAt(g_input, zx::telemetry::NowNs());
}

void NoteTicBuilt()
{
	ComputeArm(g_input);
}

void NotePacketApplied()
{
	ComputeStampAt(g_packet, zx::telemetry::NowNs());
	ComputeArm(g_packet);
}

void FrameShown()
{
	const long long now = zx::telemetry::NowNs();
	long long since = 0;
	if (ComputeTakeShown(g_input, since)) zx::telemetry::Region(InputCounter(), since, now);
	if (ComputeTakeShown(g_packet, since)) zx::telemetry::Region(PacketCounter(), since, now);
}

}} // namespace zx::latency
