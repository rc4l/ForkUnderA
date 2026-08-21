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

namespace zx { namespace surfaces {

// The vertical half of a wall, derived: the span at each end and the texture coordinate that goes
// with it. Horizontal coordinates are not answered here -- see the note above.
struct DerivedWallSpan
{
	float ztop[2];      // at the seg's v1 and v2
	float zbottom[2];
	float vTop[2];      // texture v at those tops
	float vBottom[2];
	bool  valid;

	DerivedWallSpan() : valid(false)
	{
		ztop[0] = ztop[1] = zbottom[0] = zbottom[1] = 0.f;
		vTop[0] = vTop[1] = vBottom[0] = vBottom[1] = 0.f;
	}
};

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
