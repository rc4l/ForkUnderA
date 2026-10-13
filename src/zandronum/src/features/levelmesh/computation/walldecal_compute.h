// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A decal glued to a wall as GLWall::DrawDecal places it: the height its top reference stands at for the wall part it marks, and its quad, cut to the fragment it lands on.

#ifndef ZX_WALLDECAL_COMPUTE_H
#define ZX_WALLDECAL_COMPUTE_H

#include "features/levelmesh/computation/wallcache_compute.h"   // [rc4l] kPart*, the parts a decal hangs on

namespace zx { namespace levelmesh {

// [rc4l] Which wall part a decal names (DBaseDecal's RF_REL* bits, as kDecalRel*).
enum { kDecalRelNone, kDecalRelUpper, kDecalRelLower, kDecalRelMid };

// [rc4l] What fixes a decal's height on a part: its own offset, the line's unpegging, and the planes' texture heights (front plane for a 3D floor's middle is the model sector's).
struct DecalHeightIn
{
	int rel;                  // [rc4l] kDecalRel*
	int part;                 // [rc4l] kPart*
	bool unpegTop, unpegBottom;
	double z;                 // [rc4l] the decal's Z
	double frontCeiling, frontFloor, backCeiling, backFloor;
};

// [rc4l] The height of the decal's reference line on that part, or false when it belongs to another part.
bool ComputeDecalHeight(const DecalHeightIn &in, double &zpos);

// [rc4l] Whether GL draws decals on a wall part: only on its plain walls, so a two-sided middle that blends or has see-through texels takes none.
bool ComputeDecalsOnPart(bool twoSidedMiddle, bool translucent, bool masked);

// [rc4l] A wall fragment as DrawDecal reads it: its ends, its share of the line (fracLeft..fracRight) and the sidedef's length in texels, and its top and bottom at each end.
struct DecalWall
{
	float x1, y1, x2, y2;
	float fracLeft, fracRight, texelLength;
	float ztop[2], zbottom[2];
};

// [rc4l] A decal's picture and place: its size and offsets in map units after its scale, its horizontal scale, the material's width in texels, where along the line it sits (DBaseDecal::LeftDistance, 2^30 the whole line), its reference height, and its flips.
struct DecalPicture
{
	float width, height, leftOffset, topOffset;
	float scaleX, materialWidth;
	float leftDistance;
	double zpos;
	bool flipx, flipy;
};

// [rc4l] One corner as DrawDecal's dv[]: world position (z the height) and texture coordinates.
struct DecalCorner { float x, y, z, u, v; };

// [rc4l] The decal's quad on the fragment (bottom-left, top-left, top-right, bottom-right as DrawDecal fills dv[]) and in `top` the unclipped top height GL lights it from; false when none of it lands there.
bool ComputeWallDecalQuad(const DecalWall &w, const DecalPicture &d, DecalCorner out[4], double &top);

// [rc4l] A decal's light level and relative light: full and none when it is fullbright, else its band's and its wall's.
void ComputeDecalLight(bool fullbright, int bandLevel, int relLight, int &light, int &rel);

}} // namespace zx::levelmesh

#endif // ZX_WALLDECAL_COMPUTE_H
