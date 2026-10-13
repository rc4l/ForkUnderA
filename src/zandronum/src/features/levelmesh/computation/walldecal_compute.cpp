// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/walldecal_compute.h"

#include <cmath>

namespace zx { namespace levelmesh {

bool ComputeDecalHeight(const DecalHeightIn &in, double &zpos)
{
	switch (in.rel)
	{
	case kDecalRelUpper:
		if (in.part != kDecalPartUpper) return false;
		zpos = in.z + (in.unpegTop ? in.frontCeiling : in.backCeiling);
		return true;
	case kDecalRelLower:
		if (in.part != kDecalPartLower) return false;
		zpos = in.z + (in.unpegBottom ? in.frontCeiling : in.backFloor);
		return true;
	case kDecalRelMid:
		if (in.part != kDecalPartMiddle) return false;
		zpos = in.z + (in.unpegBottom ? in.frontFloor : in.frontCeiling);
		return true;
	default:
		// [rc4l] No valid decal has another type, so GL skips one that does.
		return false;
	}
}

bool ComputeDecalsOnPart(bool twoSidedMiddle, bool translucent, bool masked)
{
	return !twoSidedMiddle || (!translucent && !masked);
}

bool ComputeWallDecalQuad(const DecalWall &w, const DecalPicture &d, DecalCorner dv[4], double &top)
{
	const float leftedge = w.fracLeft * w.texelLength;
	const float linelength = w.fracRight * w.texelLength - leftedge;
	// [rc4l] The texel index of the decal's left edge along the fragment.
	const float decalpixpos = w.texelLength * d.leftDistance / (float)(1 << 30) - (d.flipx ? d.width - d.leftOffset : d.leftOffset) - leftedge;
	float left, right, lefttex, righttex;
	if (decalpixpos < 0) { left = 0; lefttex = -decalpixpos; }
	else { left = decalpixpos; lefttex = 0; }
	if (decalpixpos + d.width > linelength) { right = linelength; righttex = right - decalpixpos; }
	else { right = decalpixpos + d.width; righttex = d.width; }
	if (right <= left) return false;

	// [rc4l] One texel along the wall as a vector.
	const float vx = (w.x2 - w.x1) / linelength, vy = (w.y2 - w.y1) / linelength;
	dv[1].x = dv[0].x = w.x1 + vx * left;
	dv[1].y = dv[0].y = w.y1 + vy * left;
	dv[3].x = dv[2].x = w.x1 + vx * right;
	dv[3].y = dv[2].y = w.y1 + vy * right;
	// [rc4l] The top offset goes onto the height in fixed point, truncated, as DrawDecal adds it.
	top = d.zpos + std::trunc(65536.0 * (d.flipy ? d.height - d.topOffset : d.topOffset)) / 65536.0;
	dv[1].z = dv[2].z = (float)top;
	dv[0].z = dv[3].z = dv[1].z - d.height;
	dv[1].v = dv[2].v = 0.f;
	dv[1].u = dv[0].u = lefttex / d.scaleX / d.materialWidth;
	dv[3].u = dv[2].u = righttex / d.scaleX / d.materialWidth;
	dv[0].v = dv[3].v = 1.f;

	// [rc4l] Clipped to the fragment's top, letting texture clamping take the extreme cases.
	const float vzt = (w.ztop[1] - w.ztop[0]) / linelength;
	const float topleft = w.ztop[0] + vzt * left, topright = w.ztop[0] + vzt * right;
	if (topleft < dv[0].z && topright < dv[3].z) return false;
	if (topleft < dv[1].z || topright < dv[2].z)
	{
		dv[1].v = (dv[1].z - topleft) / (dv[1].z - dv[0].z) * dv[0].v;
		dv[2].v = (dv[2].z - topright) / (dv[2].z - dv[3].z) * dv[3].v;
		dv[1].z = topleft;
		dv[2].z = topright;
	}
	// [rc4l] And to its bottom.
	const float vzb = (w.zbottom[1] - w.zbottom[0]) / linelength;
	const float bottomleft = w.zbottom[0] + vzb * left, bottomright = w.zbottom[0] + vzb * right;
	if (bottomleft > dv[1].z && bottomright > dv[2].z) return false;
	if (bottomleft > dv[0].z || bottomright > dv[3].z)
	{
		dv[0].v = (dv[1].z - bottomleft) / (dv[1].z - dv[0].z) * (dv[0].v - dv[1].v) + dv[1].v;
		dv[3].v = (dv[2].z - bottomright) / (dv[2].z - dv[3].z) * (dv[3].v - dv[2].v) + dv[2].v;
		dv[0].z = bottomleft;
		dv[3].z = bottomright;
	}
	if (d.flipx) for (int i = 0; i < 4; i++) dv[i].u = 1.f - dv[i].u;
	if (d.flipy) for (int i = 0; i < 4; i++) dv[i].v = 1.f - dv[i].v;
	return true;
}

}} // namespace zx::levelmesh
