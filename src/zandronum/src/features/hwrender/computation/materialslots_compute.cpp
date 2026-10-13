// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/materialslots_compute.h"

#include <string.h>

namespace zx { namespace hwrender {

unsigned ComputeMaterialSlotCapacity(const DescriptorLimits &limits)
{
	const unsigned counted[] = { limits.perStageSampledImages, limits.perStageSamplers, limits.perStageResources,
		limits.setSampledImages, limits.setSamplers };
	unsigned least = (unsigned)kMaxMaterialSlots + kOtherStageResources;
	for (unsigned c : counted) if (c < least) least = c;
	const unsigned room = least > (unsigned)kOtherStageResources ? least - kOtherStageResources : 0;
	if (room < (unsigned)kMinMaterialSlots) return 0;
	return room < (unsigned)kMaxMaterialSlots ? room : (unsigned)kMaxMaterialSlots;
}

std::string ComputeShaderWithDefines(const char *source, const std::string &defines)
{
	const char *eol = strchr(source, '\n');
	if (eol == NULL) return source;
	std::string out(source, eol + 1);
	out += defines;
	out += eol + 1;
	return out;
}

}} // namespace zx::hwrender
