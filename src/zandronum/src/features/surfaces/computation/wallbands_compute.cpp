// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/surfaces/computation/wallbands_compute.h"

namespace zx { namespace surfaces {

namespace {

void Emit(WallBand *out, int &n, int maxOut, const float *top, const float *bottom,
	int lightIndex, bool ownLight)
{
	if (n >= maxOut) return;
	// A band with no height is not a piece. SplitWall reaches this when two light planes meet, and
	// registering it would put a zero-area quad in the mesh with a real material and a real slot.
	if (!(top[0] > bottom[0] || top[1] > bottom[1])) return;
	WallBand &b = out[n++];
	b.ztop[0] = top[0]; b.ztop[1] = top[1];
	b.zbottom[0] = bottom[0]; b.zbottom[1] = bottom[1];
	b.lightIndex = lightIndex;
	b.ownLight = ownLight;
}

} // namespace

int ComputeWallBands(const float *ztop, const float *zbottom,
	const float bandBottom[][2], int nLights, WallBand *out, int maxOut)
{
	int n = 0;
	if (maxOut <= 0) return 0;
	if (nLights <= 0)
	{
		Emit(out, n, maxOut, ztop, zbottom, 0, true);
		return n;
	}

	float top[2] = { ztop[0], ztop[1] };
	for (int i = 0; i < nLights - 1; i++)
	{
		const float bl = bandBottom[i][0], br = bandBottom[i][1];

		// The light is completely above the wall: it lights nothing here.
		if (bl >= top[0] && br >= top[1]) continue;

		// ...and completely below it, so this band lights everything that is left.
		if (bl <= zbottom[0] && br <= zbottom[1])
		{
			// SplitWall's "3D floor is completely within this light": the uppermost section keeps the
			// wall's own light, every other takes the band's.
			Emit(out, n, maxOut, top, zbottom, i, i == 0);
			return n;
		}

		// The boundary cuts the wall. Everything above it belongs to this band; carry on below.
		if (bl <= top[0] && br <= top[1] && (bl != top[0] || br != top[1]))
		{
			const float cut[2] = { bl, br };
			Emit(out, n, maxOut, top, cut, i, i == 0);
			top[0] = bl; top[1] = br;
		}

		if (top[0] == zbottom[0] && top[1] == zbottom[1]) return n;
	}

	// Whatever is left is lit by the LAST band -- and through Put3DWall even when that is band 0,
	// which is why this cannot pass ownLight the way the loop does.
	Emit(out, n, maxOut, top, zbottom, nLights - 1, false);
	return n;
}

bool WallCrossesABand(const float *ztop, const float *zbottom,
	const float bandBottom[][2], int nLights)
{
	for (int i = 0; i < nLights - 1; i++)
	{
		const float bl = bandBottom[i][0], br = bandBottom[i][1];
		if (bl >= ztop[0] && br >= ztop[1]) continue;          // above the wall
		if (bl <= zbottom[0] && br <= zbottom[1]) return false; // at or below it: nothing crosses
		return true;
	}
	return false;
}

}} // namespace zx::surfaces
