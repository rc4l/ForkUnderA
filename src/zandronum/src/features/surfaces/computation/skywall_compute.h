// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GLWall::SkyNormal, SkyTop and SkyBottom: the walls GL raises from a seg to a sky, a skybox, a stacked sector or a reflective plane.

#ifndef ZX_SKYWALL_COMPUTE_H
#define ZX_SKYWALL_COMPUTE_H

namespace zx { namespace surfaces {

// [rc4l] One wall GL would hand to SkyPlane: which plane it belongs to, and its bottom and top at the line's two ends.
struct SkyWallQuad
{
	bool  ceiling;
	float zbottom[2], ztop[2];
	bool  skyhack;          // [rc4l] GLWF_SKYHACK: a mid texture on this line needs GL's special treatment
	bool  allowReflect;     // [rc4l] SkyPlane may make a plane mirror of it
};

// [rc4l] SkyNormal: a one-sided wall's wall above its ceiling and below its floor, both to the map's limit.
void ComputeSkyNormal(const float zfloor[2], const float zceil[2], SkyWallQuad out[2]);

// [rc4l] What SkyTop reads of a two-sided line: both ceilings, their skies and portals, and the side's textures.
struct SkyTopIn
{
	bool  frontSky, backSky;          // [rc4l] the ceiling texture is the sky, front and back
	bool  backNoSkyDraw;              // [rc4l] the back sector's special is NoSkyDraw
	bool  backOpen;                   // [rc4l] the back ceiling is above its floor at either end, or the back is a transdoor
	bool  icarus;                     // [rc4l] both floors flat and the back's texture z one unit above the front's
	bool  hasBottom;                  // [rc4l] a lower texture that is not TEX_Null
	bool  midExists;                  // [rc4l] TexMan has something for the mid texture
	bool  dontPegTop;                 // [rc4l] ML_DONTPEGTOP
	bool  midOffsetDown;              // [rc4l] the mid texture's y offset is above zero
	float backCeilingPlusMid;         // [rc4l] the back ceiling at v2 plus that offset
	float zceil[2];                   // [rc4l] the front ceiling at the ends, as GLWall::Process takes it
	float backCeiling[2];
	bool  frontReflect, backReflectSameHeight;   // [rc4l] the front ceiling reflects; the back does too, at the same height
	bool  frontPortal, samePortal;    // [rc4l] the front ceiling is a stacked sector's; the back's is the same portal
};

// [rc4l] What SkyBottom reads, the floors' counterpart of SkyTopIn.
struct SkyBottomIn
{
	bool  frontSky, backSky;
	bool  backNoSkyDraw;
	bool  bottomNull;                 // [rc4l] the lower texture is TEX_Null
	bool  backOpen;                   // [rc4l] the back ceiling is above its floor at either end
	bool  eyeBelowBackFloor;          // [rc4l] Vrack2b: the eye is below the back floor
	float zfloor[2];
	float backFloor[2];
	bool  frontReflect, backReflectSameHeight;
	bool  frontPortal, samePortal;
};

// [rc4l] SkyTop: the wall above a two-sided line, when GL raises one.
bool ComputeSkyTop(const SkyTopIn &in, SkyWallQuad &out);

// [rc4l] SkyBottom: the wall below a two-sided line, when GL lowers one.
bool ComputeSkyBottom(const SkyBottomIn &in, SkyWallQuad &out);

}} // namespace zx::surfaces

#endif // ZX_SKYWALL_COMPUTE_H
