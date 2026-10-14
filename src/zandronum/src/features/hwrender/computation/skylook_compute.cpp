// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/skylook_compute.h"

namespace zx { namespace hwrender {

SkyLook ComputeSkyLook(const SkyLookIn &in)
{
	SkyLook s = { -1, 0.f, 0.f, false, -1, 0.f };
	if ((in.sectorSky & kSkyFlatFlag) && (in.sectorSky & (kSkyFlatFlag - 1)))
	{
		// [rc4l] Lightning swaps to the line's lower texture when it has one.
		const bool bottom = in.swapSkies && in.lineBottomValid;
		if (bottom ? in.lineBottomDraws : in.lineTopDraws)
		{
			s.texture = bottom ? in.lineBottom : in.lineTop;
			s.xOffset = bottom ? in.lineBottomX : in.lineTopX;
			s.yOffset = bottom ? in.lineBottomY : in.lineTopY;
			s.mirrored = in.lineMirrored;
			return s;
		}
	}
	if (in.doubleSky)
	{
		s.secondTexture = in.sky1;
		s.secondXOffset = in.sky1Pos;
	}
	const bool wantsSky2 = in.swapSkies || in.sectorSky == kSkyFlatFlag || in.doubleSky;
	if (wantsSky2 && in.sky2 != in.sky1)
	{
		s.texture = in.sky2;
		s.xOffset = in.sky2Pos;
	}
	else
	{
		s.texture = in.sky1;
		s.xOffset = in.sky1Pos;
	}
	// [rc4l] GLSkyPortal::DrawContents draws no second layer of the same texture.
	if (s.secondTexture == s.texture) s.secondTexture = -1;
	return s;
}

int ComputeSkyLookPicture(SkyLook *looks, int &count, int room, const SkyLook &look, const SkyLook &level)
{
	if (ComputeSameSkyLook(look, level)) return -1;
	for (int p = 0; p < count; p++) if (ComputeSameSkyLook(look, looks[p])) return p;
	if (count >= room) return -1;
	looks[count] = look;
	return count++;
}

int ComputeSkyQuadKey(int sectorSky)
{
	if (!(sectorSky & kSkyFlatFlag)) return 0;
	const int line = sectorSky & (kSkyFlatFlag - 1);
	return line == 0 ? 1 : line + 1;
}

int ComputeSkyFromQuadKey(int key)
{
	if (key <= 0) return 0;
	return key == 1 ? kSkyFlatFlag : kSkyFlatFlag | (key - 1);
}

void ComputeSkyDrawConstants(float xOffsetDeg, bool mirrored, float yOffset, float vscale, int texh, bool secondLayer, float out[4])
{
	out[0] = (-180.0f + xOffsetDeg) * 3.14159265f / 180.0f;
	out[1] = mirrored ? -1.f : 1.f;
	out[2] = texh > 0 ? vscale * yOffset / (float)texh : 0.f;
	out[3] = secondLayer ? 1.f : 0.f;
}

bool ComputeSameSkyLook(const SkyLook &a, const SkyLook &b)
{
	return a.texture == b.texture && a.xOffset == b.xOffset && a.yOffset == b.yOffset && a.mirrored == b.mirrored &&
		a.secondTexture == b.secondTexture && (a.secondTexture < 0 || a.secondXOffset == b.secondXOffset);
}

}} // namespace zx::hwrender
