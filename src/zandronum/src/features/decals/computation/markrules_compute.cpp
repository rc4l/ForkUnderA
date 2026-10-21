// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/decals/computation/markrules_compute.h"

#include <stdlib.h>

namespace zx { namespace decals {

bool ComputeIsImpact(int source)
{
	return source == kSourceHitscan || source == kSourceRail || source == kSourceMissile || source == kSourceBlood;
}

bool ComputeRefusedBeforeChoice(const MarkRequest &request, const MarkSettings &settings)
{
	const bool byName = request.source == kSourceBlood || request.source == kSourceSpray;   // [rc4l] stock looks these up by name, behind cl_maxdecals
	return (settings.server && ComputeIsImpact(request.source)) || (request.source == kSourceMissile && !settings.missileDecals)
		|| (byName && settings.maxDecals <= 0);
}

int ComputeSurfaceVerdict(const MarkSurface &surface)
{
	const bool wall = surface.kind == kSurfaceWall;
	if (wall && surface.sideOptOut) return kMarkSideOptOut;
	if (wall && surface.horizon) return kMarkHorizon;
	if (wall && surface.open) return kMarkOpenSpan;
	if (surface.sky) return kMarkSky;
	if (surface.liquid) return kMarkLiquid;
	if (!wall && surface.portal) return kMarkPortal;
	if (surface.translucent) return kMarkTranslucent;
	if (!wall && surface.scrolls) return kMarkScrolls;
	if (surface.textureRefuses) return kMarkTextureRefuses;
	return kMarkAllowed;
}

int ComputeMarkAllowed(const MarkSurface &surface, const MarkRequest &request, const MarkSettings &settings)
{
	const bool impact = ComputeIsImpact(request.source);
	if (settings.server && impact) return kMarkServer;
	if (request.permanent) return kMarkAllowed;
	if (settings.maxDecals <= 0) return kMarkDecalsOff;
	if (request.source == kSourceMissile && !settings.missileDecals) return kMarkMissileDecalsOff;
	if (surface.kind != kSurfaceWall && !settings.flatMarks) return kMarkFlatsOff;
	// [rc4l] A script's or a spray's decal may land on a horizon line, as stock lets it; a shot never reaches one.
	MarkSurface judged = surface;
	if (!impact) judged.horizon = false;
	return ComputeSurfaceVerdict(judged);
}

const char *ComputeMarkVerdictName(int verdict)
{
	static const char *const names[kMarkVerdicts] = {
		"made", "server", "decals off", "missile decals off", "flat marks off", "noautodecals side", "horizon line",
		"open span", "sky", "liquid", "portal plane", "translucent 3D floor", "scrolling plane", "texture refuses decals" };
	return verdict >= 0 && verdict < kMarkVerdicts ? names[verdict] : "?";
}

int ComputeMissilePlaneHit(long long z, long long height, long long floorz, long long ceilingz, long long slack)
{
	if (z + height >= ceilingz - slack) return kSurfaceCeiling;
	if (z <= floorz + slack) return kSurfaceFloor;
	return kSurfaceWall;
}

int ComputePlaneOwner(long long z, const long long *heights, int count, long long slack)
{
	int best = -1;
	for (int i = 0; i < count; i++)
	{
		const long long d = llabs(heights[i] - z);
		if (d <= slack && (best < 0 || d < llabs(heights[best] - z))) best = i;
	}
	return best;
}

int ComputeMarksToEvict(int count, int max)
{
	if (max <= 0) return count;
	return count >= max ? count - max + 1 : 0;
}

bool ComputeChoiceFromGameStream(int source, int kind)
{
	return kind == kSurfaceWall || source == kSourceHitscan || source == kSourceMissile;
}

}} // namespace zx::decals
