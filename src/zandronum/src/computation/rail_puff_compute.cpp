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
	return out;
}

bool ComputeRailPuffInSky(int traceHitType, bool horizonLine, bool floorSky, bool ceilingSky, bool skyExplode)
{
	if (skyExplode) return false;
	switch (traceHitType)
	{
	case ZX_TRACE_HitWall: return horizonLine;
	case ZX_TRACE_HitFloor: return floorSky;
	case ZX_TRACE_HitCeiling: return ceilingSky;
	default: return false;
	}
}
