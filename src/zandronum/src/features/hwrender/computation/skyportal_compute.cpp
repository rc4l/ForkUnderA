// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/skyportal_compute.h"

namespace zx { namespace hwrender {

bool ComputeBeyondSkyPortalReach(float minX, float maxX, float minY, float maxY,
                                 float camX, float camY, float reach)
{
	return minX > camX + reach || maxX < camX - reach || minY > camY + reach || maxY < camY - reach;
}

void ComputePassEye(bool skyPortalPass, const float *player, const float *portal, float *out)
{
	const float *eye = skyPortalPass ? portal : player;
	for (int i = 0; i < 3; i++) out[i] = eye[i];
}

}} // namespace zx::hwrender
