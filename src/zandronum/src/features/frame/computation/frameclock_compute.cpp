// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/frameclock_compute.h"

namespace zx { namespace frame {

unsigned int ComputeFrameClock(int pinnedMs, unsigned int nowMs)
{
	return pinnedMs > 0 ? (unsigned int)pinnedMs : nowMs;
}

}} // namespace zx::frame
