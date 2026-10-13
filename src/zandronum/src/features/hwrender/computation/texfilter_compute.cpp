// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/texfilter_compute.h"

namespace zx { namespace hwrender {

TextureFilter ComputeTextureFilter(int mode)
{
	if (mode < 0 || mode > 5) mode = 0;
	TextureFilter f;
	f.minLinear = f.magLinear = (mode == 2 || mode == 3 || mode == 4);
	f.mipLinear = (mode == 4 || mode == 5);
	f.mipmapped = (mode != 0 && mode != 2);
	return f;
}

unsigned ComputeAnisotropy(const TextureFilter &filter, float setting)
{
	if (!filter.minLinear || !filter.magLinear || !filter.mipmapped || !(setting >= 1.5f)) return 0;
	return setting >= 16.f ? 16u : (unsigned)(setting + 0.5f);
}

}} // namespace zx::hwrender
