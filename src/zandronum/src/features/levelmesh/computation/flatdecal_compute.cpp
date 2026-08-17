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

float ComputeDecalHeight(float hitZ, float planeZAtSpawn, float planeZNow, bool ceiling,
                         float offset)
{
	return hitZ + (planeZNow - planeZAtSpawn) + (ceiling ? -offset : offset);
}

}} // namespace zx::levelmesh
