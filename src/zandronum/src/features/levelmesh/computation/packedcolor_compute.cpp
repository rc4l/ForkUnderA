// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/packedcolor_compute.h"

namespace zx { namespace levelmesh {

void ComputeUnpackRGB(unsigned int packed, float &r, float &g, float &b)
{
	r = ((packed >> 16) & 0xff) / 255.f;
	g = ((packed >> 8) & 0xff) / 255.f;
	b = (packed & 0xff) / 255.f;
}

}} // namespace zx::levelmesh
