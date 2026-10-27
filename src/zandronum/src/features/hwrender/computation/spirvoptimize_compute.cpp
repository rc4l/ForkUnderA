// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/hwrender/computation/spirvoptimize_compute.h"

namespace zx { namespace hwrender {

bool ComputeOptimizeSpirv(int setting, bool webgpu)
{
	if (setting == 0 || setting == 1) return setting == 1;
	return webgpu;
}

}} // namespace zx::hwrender
