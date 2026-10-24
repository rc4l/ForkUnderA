// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/latency/computation/latency_compute.h"

namespace zx { namespace latency {

void ComputeStampAt(LatencyStamp &stamp, long long nowNs)
{
	if (stamp.since == 0) stamp.since = nowNs;
}

void ComputeArm(LatencyStamp &stamp)
{
	if (stamp.since != 0) stamp.armed = true;
}

bool ComputeTakeShown(LatencyStamp &stamp, long long &sinceNs)
{
	if (!stamp.armed) return false;
	sinceNs = stamp.since;
	stamp = LatencyStamp();
	return true;
}

}} // namespace zx::latency
