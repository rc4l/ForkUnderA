// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/skyportal_compute.h"

namespace zx { namespace hwrender {

bool ComputeBeyondSkyPortalReach(float minX, float maxX, float minY, float maxY,
                                 float camX, float camY, float reach)
{
	return minX > camX + reach || maxX < camX - reach || minY > camY + reach || maxY < camY - reach;
}

}} // namespace zx::hwrender
