// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_WATERLEVEL_COMPUTE_H
#define ZX_WATERLEVEL_COMPUTE_H

// [Doruk-Aksoy] What AActor::UpdateWaterLevel leaves in waterlevel and boomwaterlevel, and whether it splashes first.
struct WaterStore
{
	int water;
	int boom;
	bool splash;
};

// [Doruk-Aksoy] The stores of an update that measured `measured`, where a non-swimmable sector's reset keeps the old waterlevel; inline, as it runs on every move.
inline WaterStore ComputeWaterStore(int lastWater, int lastBoom, int measured, bool reset, bool dosplash)
{
	WaterStore s;
	s.water = reset ? lastWater : measured;
	s.boom = measured;
	s.splash = lastBoom == 0 && measured != 0 && dosplash;
	return s;
}

#endif
