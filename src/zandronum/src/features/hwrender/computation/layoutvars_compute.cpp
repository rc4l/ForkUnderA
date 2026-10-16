// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/layoutvars_compute.h"

namespace zx { namespace hwrender {

std::vector<LayoutVar> ComputeFlagStorageBuffers(const std::vector<LayoutVar> &vars, const std::vector<StageBuffer> &buffers,
	int defaultType, unsigned flag)
{
	std::vector<LayoutVar> out = vars;
	for (const StageBuffer &b : buffers)
	{
		bool listed = false;
		for (LayoutVar &v : out)
		{
			if ((v.stages & b.stage) != 0 && v.name == b.name)
			{
				v.flags |= flag;
				listed = true;
			}
		}
		if (!listed) out.push_back({ b.stage, b.name, defaultType, flag });
	}
	return out;
}

}} // namespace zx::hwrender
