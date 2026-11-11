// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/frame/computation/framestall_compute.h"

namespace zx { namespace frame {

int ComputeStallUs(int askedUs)
{
	return askedUs > 0 ? askedUs : 0;
}

}} // namespace zx::frame
