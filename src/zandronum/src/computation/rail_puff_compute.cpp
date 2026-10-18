// [rc4l] Implementation of the pure rail-endpoint spawn decision. See rail_puff_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "rail_puff_compute.h"

RailEndpointSpawn ComputeRailEndpointSpawn(int traceHitType)
{
	RailEndpointSpawn out;
	out.puff = (traceHitType == ZX_TRACE_HitWall
		|| traceHitType == ZX_TRACE_HitFloor
		|| traceHitType == ZX_TRACE_HitCeiling);
	out.decal = out.puff;   // [rc4l] features/decals marks any surface and refuses what refuses, sky flats included
	out.skyGuard = (traceHitType == ZX_TRACE_HitFloor
		|| traceHitType == ZX_TRACE_HitCeiling);
	return out;
}
