// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/fogboundary_compute.h"

namespace zx { namespace levelmesh {

namespace {

// [rc4l] Whether a side's fog is the outside fog, the level's density, or neither (gl_CheckFog's cases 2, 3 and 4).
enum { kFogNone, kFogOutside, kFogLevel, kFogLight };

int FogCase(const FogSide &s, const FogRules &rules)
{
	if ((s.fadeRGB & 0xffffff) == 0) return kFogNone;
	if (rules.outsideFogDensity != 0 && (rules.outsideFogColor >> 24) != 0xff && (s.fadeRGB & 0xffffff) == (rules.outsideFogColor & 0xffffff))
		return kFogOutside;
	if (rules.fogDensity != 0 || rules.lightModeFog) return kFogLevel;
	return kFogLight;
}

float Max(float a, float b) { return a > b ? a : b; }
float Min(float a, float b) { return a < b ? a : b; }

} // namespace

bool ComputeFogBoundary(const FogSide &front, const FogSide &back, const FogRules &rules, bool sameSector)
{
	if (sameSector) return false;
	const int f = FogCase(front, rules);
	if (f == kFogNone || (f == kFogLight && front.lightLevel >= 248)) return false;
	const int b = FogCase(back, rules);
	if (b == kFogOutside || b == kFogLevel) return false;
	if (b == kFogLight && back.lightLevel < 248) return false;
	return true;
}

bool ComputeFogSheetOpening(const MidOpeningIn &in, float top[2], float bottom[2])
{
	for (int e = 0; e < 2; e++)
	{
		// [rc4l] An intra-sky line with no upper is not clipped at all, which with no middle texture is height 0, as GL's texturetop is.
		if (in.upperMissing) top[e] = (in.frontCeilingSky && in.backCeilingSky) ? 0.f : Max(in.backCeiling[e], in.frontCeiling[e]);
		else if ((in.backCeiling[0] > in.frontCeiling[0] || in.backCeiling[1] > in.frontCeiling[1]) && (!in.frontCeilingSky || in.backCeilingSky))
			top[e] = in.backCeiling[e];
		else top[e] = Min(in.backCeiling[e], in.frontCeiling[e]);

		if (in.lowerMissing) bottom[e] = Min(in.backFloor[e], in.frontFloor[e]);
		else if (in.backFloor[0] < in.frontFloor[0] || in.backFloor[1] < in.frontFloor[1]) bottom[e] = in.backFloor[e];
		else bottom[e] = Max(in.backFloor[e], in.frontFloor[e]);
	}
	return !(top[0] <= bottom[0] && top[1] <= bottom[1]);
}

}} // namespace zx::levelmesh
