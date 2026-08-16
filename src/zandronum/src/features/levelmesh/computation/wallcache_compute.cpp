// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/wallcache_compute.h"

namespace zx { namespace levelmesh {

bool ComputeStampsEqual(const WallCacheStamp &a, const WallCacheStamp &b)
{
	return a.frontDirty == b.frontDirty
		&& a.backDirty  == b.backDirty
		&& a.sideDirty  == b.sideDirty;
}

bool ComputeIsCacheable(const WallCacheEligibility &e)
{
	return !e.isPolyobject
		&& !e.hasHeightsec
		&& !e.hasFFloors
		&& !e.producesPortal
		&& !e.inArea;
}

bool ComputeCanReuse(const WallCacheEligibility &e, bool haveCached,
                     const WallCacheStamp &cached, const WallCacheStamp &current)
{
	if (!haveCached) return false;
	if (!ComputeIsCacheable(e)) return false;
	return ComputeStampsEqual(cached, current);
}


}} // namespace zx::levelmesh
