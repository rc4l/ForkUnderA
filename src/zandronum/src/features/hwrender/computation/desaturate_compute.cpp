// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/desaturate_compute.h"

namespace zx { namespace hwrender {

void ComputeDesaturate(float rgb[3], float factor)
{
	if (factor <= 0.f) return;
	const float gray = rgb[0] * 0.3f + rgb[1] * 0.56f + rgb[2] * 0.14f;
	for (int c = 0; c < 3; c++) rgb[c] += (gray - rgb[c]) * factor;
}

float ComputeDesaturationFactor(int amount) { return amount / 255.f; }

}} // namespace zx::hwrender
