// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/walllight_compute.h"

namespace zx { namespace hwrender {

const int kNoWallLightIndex = -1;

int ComputeWallLightIndex(bool lightPassRan, int computedThisFrame, int carriedOver)
{
	(void)carriedOver;
	if (!lightPassRan) return kNoWallLightIndex;
	return (computedThisFrame < 0) ? kNoWallLightIndex : computedThisFrame;
}

}} // namespace zx::hwrender
