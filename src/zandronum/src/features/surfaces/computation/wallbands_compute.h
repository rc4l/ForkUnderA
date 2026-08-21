// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Cutting a wall into the light bands a 3D floor casts on it.
//
// A sector with 3D floors does not have ONE light level. Each floor's control sector contributes a
// band, and the wall behind it is lit by whichever band it falls in -- so a wall crossing two floors
// is drawn as three pieces with three different lights and colormaps. GLWall::SplitWall does this by
// recursion, cutting a copy of itself at each band boundary and putting each piece separately.
//
// This is that rule, as a function over numbers. It is why the map-driven bake could not carry those
// sectors and had to hand them back to the capture -- and handing them back is exactly what makes
// them vanish when GL stops walking the level, which is the thing standing between this renderer and
// not needing GL at all.
//
// Engine-free: heights in, spans out. No seg, no sector, no lightlist_t.

#ifndef ZX_WALLBANDS_COMPUTE_H
#define ZX_WALLBANDS_COMPUTE_H

namespace zx { namespace surfaces {

// One piece of a wall, lit by one band.
struct WallBand
{
	float ztop[2], zbottom[2];
	// Which entry of the sector's light list lights it.
	int   lightIndex;
	// [rc4l] Whether this piece takes the wall's OWN light or the band's.
	//
	// SplitWall puts the uppermost section with PutWall and every other with Put3DWall, and the two
	// differ by more than the light level: Put3DWall also copies the band's colormap. The last piece
	// always goes through Put3DWall even when it is band 0, so "is it band 0" is not the question --
	// "did it come out of the loop or off the end" is.
	bool  ownLight;
};

// [rc4l] Split a wall at its light boundaries, top down.
//
// `bandBottom[i]` is the bottom plane of band i evaluated at the wall's two ends -- that is
// lightlist[i+1]'s plane, because a band is bounded below by the next one down. `nLights` is the
// size of the light list; a list of n lights yields at most n bands.
//
// Returns how many bands were written. Zero means the wall has no area to give.
int ComputeWallBands(const float *ztop, const float *zbottom,
	const float bandBottom[][2], int nLights, WallBand *out, int maxOut);

// [rc4l] Does this wall need cutting at all, or is one piece the whole answer?
//
// Asked separately because the common case by far is that it does not -- a wall entirely inside one
// band -- and the caller can then keep its single-piece path rather than allocating for a list.
bool WallCrossesABand(const float *ztop, const float *zbottom,
	const float bandBottom[][2], int nLights);

}} // namespace zx::surfaces

#endif // ZX_WALLBANDS_COMPUTE_H
