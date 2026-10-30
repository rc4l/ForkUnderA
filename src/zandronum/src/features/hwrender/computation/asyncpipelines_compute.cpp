// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/asyncpipelines_compute.h"

namespace zx { namespace hwrender {

bool ComputeAsyncPipelines(bool wanted, bool canWaitForGPU)
{
	return wanted && canWaitForGPU;
}

}} // namespace zx::hwrender
