// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/gamecopy/computation/cullpoint_compute.h"

#include <algorithm>

namespace zx { namespace gamecopy {

ActorCullPoint ComputeActorCullPoint(bool inSector, float x, float y, float z, float height, float radius, float modelReach)
{
	if (!inSector) return ActorCullPoint{ x, z, y, -1.f };
	return ActorCullPoint{ x, z + height * 0.5f, y, std::max(radius, modelReach) + height + 64.f };
}

}} // namespace zx::gamecopy
