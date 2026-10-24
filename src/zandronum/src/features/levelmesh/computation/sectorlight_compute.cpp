// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/sectorlight_compute.h"

namespace zx { namespace levelmesh {

namespace {

int Clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

} // namespace

SectorLitInput ComputeSectorLitInput(const SectorLightEntry &e, const SectorLightRef &r, bool fadeTable)
{
	SectorLitInput in;
	in.lightColor = e.lightColor;
	in.fadeColor = e.fadeColor;
	in.blendFactor = e.blendFactor;
	in.desaturation = e.desaturation;
	if (r.kind == kSectorLitFloor) { in.lightLevel = e.floorLight; return in; }
	if (r.kind == kSectorLitCeiling) { in.lightLevel = e.ceilingLight; return in; }
	const int org = Clamp255(e.lightLevel);
	const bool foggy = (e.fadeColor & 0xffffff) != 0 || fadeTable;
	int base = r.sideAbsolute ? r.sideLight : org;
	const int rel = foggy ? 0 : r.contrast;
	if (!r.sideAbsolute && (!foggy || r.sideLightFog)) base = Clamp255(base + r.sideLight);
	in.lightLevel = Clamp255(base);
	if (org >= 253) in.relLight = 0;
	else if (in.lightLevel - rel > 256) in.relLight = 256 - in.lightLevel + rel;
	else in.relLight = rel;
	return in;
}

bool ComputeSectorLitReproduces(const SectorLitInput &in, int lightLevel, int relLight, unsigned lightColor, unsigned fadeColor, int blendFactor, int desaturation)
{
	return in.lightLevel == lightLevel && in.relLight == relLight && in.lightColor == lightColor && in.fadeColor == fadeColor &&
		in.blendFactor == blendFactor && in.desaturation == desaturation;
}

void ComputeSectorLightWords(const SectorLightEntry &e, unsigned out[kSectorLightWords])
{
	out[0] = (unsigned)e.lightLevel;
	out[1] = (unsigned)e.floorLight;
	out[2] = (unsigned)e.ceilingLight;
	out[3] = e.lightColor;
	out[4] = e.fadeColor;
	out[5] = (unsigned)e.blendFactor;
	out[6] = (unsigned)e.desaturation;
	out[7] = 0;
}

}} // namespace zx::levelmesh
