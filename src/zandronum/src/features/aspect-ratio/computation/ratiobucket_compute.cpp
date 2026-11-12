// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/aspect-ratio/computation/ratiobucket_compute.h"

#include <math.h>
#include <stddef.h>

namespace zx { namespace aspect {

int ComputeRatioBucket(int width, int height)
{
	if (height <= 0)
		return 0;

	// [rc4l] Ported from zandronum@a48630d214dc2d2e185bccaa764f7b9a911a1602: the nearest ratio wins, so every shape has a bucket; fabsf, since abs on a float can be the int one.
	static const struct { float ratio; int bucket; } ratioTypes[] =
	{
		{ 21 / 9.0f, 6 },
		{ 16 / 9.0f, 1 },
		{ 17 / 10.0f, 3 },
		{ 16 / 10.0f, 2 },
		{ 4 / 3.0f, 0 },
		{ 5 / 4.0f, 4 },
	};
	const float aspect = width / (float)height;
	int bucket = ratioTypes[0].bucket;
	float distance = fabsf(ratioTypes[0].ratio - aspect);
	for (size_t i = 1; i < sizeof(ratioTypes) / sizeof(ratioTypes[0]); i++)
	{
		const float d = fabsf(ratioTypes[i].ratio - aspect);
		if (d < distance)
		{
			bucket = ratioTypes[i].bucket;
			distance = d;
		}
	}
	return bucket;
}

int ComputeForcedRatioBucket(int vidAspect, bool noWidescreen, bool tft, int width, int height)
{
	if (noWidescreen)
		return (tft && height * 5/4 == width) ? 4 : 0;

	// [SP] User wants to force aspect ratio; let them; vid_aspect 3 is 4:3 and 5 is 17:10.
	if (vidAspect >= 1 && vidAspect <= 6)
	{
		if (vidAspect == 3)
			return 0;
		if (vidAspect == 5)
			return 3;
		return vidAspect;
	}
	return -1;
}

bool ComputeRatioIsWidescreen(int bucket)
{
	return (bucket & 3) != 0;
}

int ComputeMenuRatioFilter(int menuScreenRatios, bool tft)
{
	if (menuScreenRatios < 0 || menuScreenRatios > 6)
		return -1;
	if (menuScreenRatios == 4 && !tft)
		return 0;
	return menuScreenRatios;
}

}} // namespace zx::aspect
