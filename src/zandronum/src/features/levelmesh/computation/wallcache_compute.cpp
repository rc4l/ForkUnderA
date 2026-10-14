// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/wallcache_compute.h"
#include "features/surfaces/walltypes.h"   // [rc4l] RENDERWALL_*

namespace zx { namespace levelmesh {

int ComputeSectorRebake(const SectorLook &before, const SectorLook &now)
{
	if (now.floorD != before.floorD || now.ceilD != before.ceilD) return kSectorMoved;
	if (now.floorPic != before.floorPic || now.ceilPic != before.ceilPic) return kSectorRetextured;
	if (now.light != before.light || now.colormap != before.colormap) return kSectorRelit;
	return kSectorSame;
}

bool ComputeTakeChange(int &seen, int now)
{
	if (seen == now) return false;
	seen = now;
	return true;
}

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

bool ComputeWallPlainPass(int renderWallType)
{
	return renderWallType == kRenderWallTop || renderWallType == kRenderWallM1S ||
		renderWallType == kRenderWallBottom || renderWallType == kRenderWallMirrorSurface;
}

bool ComputeIsMirrorWall(int lineSpecial, bool oneSided, bool mirrorsOn)
{
	return lineSpecial == kLineMirror && oneSided && mirrorsOn;
}

SideChange ComputeSideChange(const SideLook &baked, const SideLook &now)
{
	// [rc4l] A side's light (Transfer_WallLight) or flags change how its walls are lit, which a scroll cannot carry.
	if (baked.light != now.light || baked.flags != now.flags) return kSideRebuild;
	bool scrolled = false;
	for (int p = 0; p < 3; p++)
	{
		if (baked.tex[p] != now.tex[p] || baked.xscale[p] != now.xscale[p] || baked.yscale[p] != now.yscale[p]) return kSideRebuild;
		if (baked.xoff[p] != now.xoff[p] || baked.yoff[p] != now.yoff[p]) scrolled = true;
	}
	return scrolled ? kSideScrolled : kSideSame;
}

bool ComputeScrollInRange(float du, float dv)
{
	return du <= kMaxScrollShift && du >= -kMaxScrollShift && dv <= kMaxScrollShift && dv >= -kMaxScrollShift;
}


int ComputePartRenderType(int part, bool twoSided)
{
	switch (part)
	{
	case kPartUpper:  return RENDERWALL_TOP;
	case kPartLower:  return RENDERWALL_BOTTOM;
	case kPartMiddle: return twoSided ? RENDERWALL_M2S : RENDERWALL_M1S;
	default:          return RENDERWALL_NONE;
	}
}

}} // namespace zx::levelmesh
