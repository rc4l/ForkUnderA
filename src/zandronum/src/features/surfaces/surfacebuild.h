// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Build a wall surface from the MAP, and put it in the mesh.
//
// features/surfaces has been able to work out what a sidedef looks like for a while, and until now
// the only thing that read the answer was the scorer. That made the derivation a claim: three
// ladders said it agreed with GL on between 79% and 100% of pieces depending on which question was
// asked, and nothing in the renderer was any different for it.
//
// This is the first piece of it that is load-bearing. It derives the two things the ladders actually
// measure -- the vertical SPAN of a wall part, and where the picture sits on it -- and hands them to
// the bake in place of what GLWall computed. Everything else about the piece still comes from the
// capture, deliberately:
//
//   - the HORIZONTAL coordinate depends on how a seg sits along its linedef, which is bookkeeping
//     the derivation does not model and no ladder measures. Deriving it would be guessing, and
//     guessing at alignment is what cost two days.
//   - the shading comes from CaptureShading, which is Phase 2b's last item and deliberately last:
//     it is the part where a second implementation drifted before.
//
// So a wall drawn this way is GL's wall with the derivation's heights and its texture's vertical
// position. That is a small enough step that the existing ladders predict exactly what should
// change, which is the point: if the picture moves anywhere the ladders said it would not, the
// derivation is wrong in a way nobody had measured.

#ifndef ZX_SURFACES_SURFACEBUILD_H
#define ZX_SURFACES_SURFACEBUILD_H

struct seg_t;
struct sector_t;

namespace zx { namespace surfaces {

// The vertical half of a wall, derived: the span at each end and the texture coordinate that goes
// with it. Horizontal coordinates are not answered here -- see the note above.
struct DerivedWallSpan
{
	float ztop[2];      // at the seg's v1 and v2
	float zbottom[2];
	float vTop[2];      // texture v at those tops
	float vBottom[2];
	// [rc4l] The horizontal coordinate, at the same two ends.
	//
	// Derivable exactly, and it took reading GLWall::Process to see why: an ordinary wall is drawn
	// over the whole LINEDEF with fracleft 0 and fracright 1, not per seg. So the left edge is the
	// sidedef's x offset and the right edge is that plus the line's texel length, with no
	// seg-along-line bookkeeping involved at all. Polyobjects are the exception and are not derived.
	float uLeft, uRight;
	bool  hasU;
	// [rc4l] The two ends in world space, taken from the LINEDEF and ordered by which side this
	// sidedef is -- which is how GLWall::Process orders them, and getting it backwards mirrors every
	// texture on that wall.
	float x1, y1, x2, y2;
	// [rc4l] What to draw it with -- the sidedef's texture, or the sector's flat where GL falls back
	// to that on a sloped step. An FMaterial*, kept as void* so this header stays free of the
	// texture system.
	const void *material;
	// Its own height, which the two-sided middle needs and the batcher wants for animation.
	const void *baseTex;
	bool  valid;

	DerivedWallSpan() : uLeft(0.f), uRight(0.f), hasU(false),
		x1(0.f), y1(0.f), x2(0.f), y2(0.f), material(0), baseTex(0), valid(false)
	{
		ztop[0] = ztop[1] = zbottom[0] = zbottom[1] = 0.f;
		vTop[0] = vTop[1] = vBottom[0] = vBottom[1] = 0.f;
	}
};

// [rc4l] The three shading INPUTS for a wall, derived from the map.
//
// Worth being precise about what this is and is not. The shading itself was never implemented twice:
// CaptureShading calls the engine's own gl_SetColor and gl_SetFog and reads the answer back out of
// FRenderState, which is exactly what stopped a second implementation drifting. What came from GLWall
// was the three INPUTS to it -- the light level after fake contrast, the relative light, and the
// colormap -- and those are sector and sidedef data.
//
// So deriving this adds no second opinion about lighting. It removes the last reason a wall's
// appearance needs GL to have walked the BSP first.
struct DerivedWallLight
{
	int       lightLevel;
	int       relLight;
	bool      valid;
	DerivedWallLight() : lightLevel(0), relLight(0), valid(false) {}
};

// Fills in the light and hands back the sector whose colormap goes with it. False for anything with
// a 3D floor light list, which splits a wall into bands and is the capture's business.
bool BuildDerivedWallLight(const seg_t *seg, DerivedWallLight &out, const sector_t *&colormapFrom);

// renderType is a RENDERWALL_* value. Answers only for the three ordinary sidedef parts; anything
// else -- a 3D floor face, a sky, a horizon -- returns false and keeps whatever the capture made.
bool BuildDerivedWallSpan(const seg_t *seg, int renderType, DerivedWallSpan &out);

// How many pieces the bake derived, and how many fell back, since the level loaded. A coverage
// figure rather than a pass: this gets finished by moving the largest remaining category across, and
// the number is what says which category that is.
void GetDeriveStats(int &derived, int &fellBack);
void ResetDeriveStats();
void NoteDeriveFallback();
void NoteDeriveSeamFallback();
// [rc4l] Why the fallbacks fell back, so the next category to build is a number rather than a hunch.
void GetDeriveFallbacks(int &twoSidedMiddle, int &special, int &noTexture, int &noSpan, int &seam);

}} // namespace zx::surfaces

#endif
