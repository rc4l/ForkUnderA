// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/skyfog_compute.h"

namespace zx { namespace hwrender {

SkyFog ComputeSkyFog(int skyfog, bool fixedColormap, unsigned int fadeRGB)
{
	SkyFog f = { 0, 0, 0, 0.f };
	if (skyfog <= 0 || fixedColormap || (fadeRGB & 0xffffff) == 0) return f;
	f.r = (fadeRGB >> 16) & 0xff;
	f.g = (fadeRGB >> 8) & 0xff;
	f.b = fadeRGB & 0xff;
	f.a = (skyfog > 255 ? 255 : skyfog) / 255.f;
	return f;
}

}} // namespace zx::hwrender
