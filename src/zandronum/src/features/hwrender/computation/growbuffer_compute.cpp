// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/growbuffer_compute.h"

namespace zx { namespace hwrender {

unsigned int ComputeGrownCapacity(unsigned int needed, unsigned int pad)
{
	return needed + needed / 2 + pad;
}

}} // namespace zx::hwrender
