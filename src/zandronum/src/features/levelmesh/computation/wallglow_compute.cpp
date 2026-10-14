// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/wallglow_compute.h"

namespace zx { namespace levelmesh {

bool ComputeWallTakesGlow(unsigned lightColorRGB, int lightLevel)
{
	return !((lightColorRGB & 0xffffff) == 0xffffff && lightLevel == 255);
}

void ComputeGlowDistances(const float top[4], const float bottom[4], float x, float height, float y, float &belowTop, float &aboveBottom)
{
	belowTop = -((top[3] + top[0] * x + top[1] * y) * top[2]) - height;
	aboveBottom = height + ((bottom[3] + bottom[0] * x + bottom[1] * y) * bottom[2]);
}

float ComputeGlowFactor(float height, float dist)
{
	if (!(height > 0.f) || !(dist < height)) return 0.f;
	return 1.f - dist / height;
}

}} // namespace zx::levelmesh
