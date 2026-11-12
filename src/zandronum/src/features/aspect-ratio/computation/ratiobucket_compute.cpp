// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/aspect-ratio/computation/ratiobucket_compute.h"

#include <stdlib.h>

namespace zx { namespace aspect {

int ComputeRatioBucket(int width, int height, bool tft)
{
	// If the size is approximately 16:9, consider it so.
	if (abs (height * 16/9 - width) < 10)
		return 1;
	// Consider 17:10 as well.
	if (abs (height * 17/10 - width) < 10)
		return 3;
	// 16:10 has more variance in the pixel dimensions. Grr.
	if (abs (height * 16/10 - width) < 60)
	{
		// 320x200 and 640x400 are always 4:3, not 16:10
		if ((width == 320 && height == 200) || (width == 640 && height == 400))
			return 0;
		return 2;
	}
	// Unless vid_tft is set, 1280x1024 is 4:3, not 5:4.
	if (height * 5/4 == width && tft)
		return 4;
	// [rc4l] Ported from zandronum@bab77c3aab00b84c18fc1fb997af3ef2776d8f07: 21:9 (actually 64:27, 21:9 is a semi-accurate ratio used in marketing).
	if (abs (height * 64/27 - width) < 30)
		return 6;
	// Assume anything else is 4:3. (Which is probably wrong these days...)
	return 0;
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
