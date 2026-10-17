// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/blend_compute.h"

namespace zx { namespace gamecopy {

zx::Fixed ComputeBlendFixed(zx::Fixed old, zx::Fixed cur, zx::Fixed smoothratio)
{
	return old + FixedMul(cur - old, smoothratio);
}

void Blends::Clear()
{
	planes.clear();
	scrolls.clear();
	walls.clear();
	polys.clear();
}

bool Blends::Empty() const
{
	return planes.empty() && scrolls.empty() && walls.empty() && polys.empty();
}

}} // namespace zx::gamecopy
