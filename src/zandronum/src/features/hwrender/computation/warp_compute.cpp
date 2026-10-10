// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/warp_compute.h"

#include <math.h>

namespace zx { namespace hwrender {

int ComputeWarpStyle(int bWarped)
{
	return (bWarped == 1 || bWarped == 2) ? bWarped : 0;
}

float ComputeWarpTimer(unsigned int frameMS, float speed)
{
	return (float)((double)frameMS * speed / 1000.0);
}

void ComputeWarpOffset(int style, float u, float v, float timer, float *du, float *dv)
{
	const float twoPi = 2.f * 3.14159265358979323846f;
	if (style == 1)
	{
		*dv = sinf(twoPi * (u + timer * 0.125f)) * 0.1f;
		*du = sinf(twoPi * (v + timer * 0.125f)) * 0.1f;
	}
	else if (style == 2)
	{
		const float siny = sinf(twoPi * (v * 2.2f + timer * 0.75f)) * 0.03f;
		*dv = siny + sinf(twoPi * (u * 0.75f + timer * 0.75f)) * 0.03f;
		*du = siny + sinf(twoPi * (u * 1.1f + timer * 0.45f)) * 0.02f;
	}
	else
	{
		*du = *dv = 0.f;
	}
}

}} // namespace zx::hwrender
