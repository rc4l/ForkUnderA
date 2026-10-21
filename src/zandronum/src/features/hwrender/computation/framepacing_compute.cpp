// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/framepacing_compute.h"

namespace zx { namespace hwrender {

unsigned long long ComputeFenceWaitValue(unsigned long long signalled, int maxAhead)
{
	if (maxAhead <= 0 || signalled <= (unsigned long long)maxAhead) return 0;
	return signalled - (unsigned long long)maxAhead;
}

int ComputePresentInterval(bool vsync, bool pagePaced)
{
	return vsync || pagePaced ? 1 : 0;
}

}} // namespace zx::hwrender
