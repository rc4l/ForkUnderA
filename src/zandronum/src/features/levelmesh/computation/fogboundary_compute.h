// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's fog boundary sheet: whether a line between two sectors gets one (gl_CheckFog) and the opening it covers (GLWall::DoMidTexture with drawfogboundary).

#ifndef ZX_FOGBOUNDARY_COMPUTE_H
#define ZX_FOGBOUNDARY_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] One side of the line as gl_CheckFog reads it: its fade colour (0xRRGGBB) and light level.
struct FogSide
{
	unsigned fadeRGB;
	int      lightLevel;
};

// [rc4l] The level's fog rules gl_CheckFog reads.
struct FogRules
{
	int      outsideFogDensity;
	unsigned outsideFogColor;   // [rc4l] the PalEntry, its alpha byte 0xff when unset
	int      fogDensity;
	bool     lightModeFog;      // [rc4l] glset.lightmode & 4
};

// [rc4l] gl_CheckFog(front, back): a fogged front sector beside an unfogged back one, the two different sectors.
bool ComputeFogBoundary(const FogSide &front, const FogSide &back, const FogRules &rules, bool sameSector);

// [rc4l] The heights DoMidTexture reads at a line's two ends, and which of its upper and lower textures are missing and which ceilings are sky.
struct MidOpeningIn
{
	float frontCeiling[2], frontFloor[2], backCeiling[2], backFloor[2];
	bool  upperMissing, lowerMissing;
	bool  frontCeilingSky, backCeilingSky;
};

// [rc4l] The opening a fog sheet covers at each end, top and bottom, by DoMidTexture's rules for an untextured middle; false when nothing of it is visible.
bool ComputeFogSheetOpening(const MidOpeningIn &in, float top[2], float bottom[2]);

}} // namespace zx::levelmesh

#endif // ZX_FOGBOUNDARY_COMPUTE_H
