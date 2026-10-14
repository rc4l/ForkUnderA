// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/framestall_compute.h"

namespace zx { namespace frame {

int ComputeFrameStallUs(bool ticRan, int ticStallUs, int everyFrameUs)
{
	const int tic = ticRan && ticStallUs > 0 ? ticStallUs : 0;
	return tic + (everyFrameUs > 0 ? everyFrameUs : 0);
}

}} // namespace zx::frame
