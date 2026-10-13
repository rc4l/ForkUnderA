// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/framepacing_compute.h"

namespace zx { namespace hwrender {

unsigned long long ComputeFenceWaitValue(unsigned long long signalled, unsigned long long maxAhead)
{
	if (maxAhead == 0 || signalled <= maxAhead) return 0;
	return signalled - maxAhead;
}

}} // namespace zx::hwrender
