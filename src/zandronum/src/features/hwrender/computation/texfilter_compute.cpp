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

}} // namespace zx::hwrender
