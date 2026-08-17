// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/flatdecal_compute.h"

namespace zx { namespace levelmesh {

bool ComputeDecalUsesTopPlane(bool hitCeiling)
{
	// A shot landing on a walkable surface marks the 3D floor's TOP; one hitting an underside marks
	// its BOTTOM.
	return !hitCeiling;
}

bool ComputeDecalTracksPlane(float hitZ, float planeZAtSpawn, float tolerance)
{
	const float d = hitZ - planeZAtSpawn;
	return (d < 0.f ? -d : d) <= tolerance;
}

float ComputeDecalHeight(float hitZ, float planeZAtSpawn, float planeZNow, bool ceiling,
                         float offset, float trackTolerance)
{
	const float travelled = ComputeDecalTracksPlane(hitZ, planeZAtSpawn, trackTolerance)
		? (planeZNow - planeZAtSpawn) : 0.f;
	return hitZ + travelled + (ceiling ? -offset : offset);
}

}} // namespace zx::levelmesh
