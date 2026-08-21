// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] What changed about a surface, and what the renderer has to do about it.
//
// A baked surface is a cache of a function of mutable level state, and Doom's level state has no
// version on it: the software renderer re-read every sidedef every frame, so nothing ever needed to
// announce that it had changed. Everything that caches must therefore invent its own notification,
// and this renderer invented four of them -- a per-seg stamp, a dirty vertex range, a layout
// generation, and a set of hand-written field comparisons -- each knowing about a different subset
// of what a surface is.
//
// A change then arrives only if it happens to travel down the right channel, and three separate
// faults in one afternoon were changes that did not:
//
//   - a switch swapped its sidedef's texture: the stamp caught it and re-baked the piece, no vertex
//     moved so the dirty range was empty, and the layout comparison did not look at the material.
//     The switch stayed looking unpressed.
//   - an animated texture reached its next frame: nothing above notices at all, and the backend had
//     to re-resolve every batch itself every frame to compensate.
//   - a door revealed a room: the geometry appeared, and the appearance of the pieces around it was
//     never reconsidered.
//
// So this is the one place that answers the question, off-engine and under test. It does not care
// where a surface came from -- a sidedef part and a sector plane produce the same record and get the
// same answer, which is the whole point of retiring the wall/flat split.

#ifndef ZX_SURFACES_SURFACECHANGE_COMPUTE_H
#define ZX_SURFACES_SURFACECHANGE_COMPUTE_H

namespace zx { namespace surfaces {

// [rc4l] What a surface looks like to a renderer, reduced to the things a renderer acts on.
//
// Deliberately not the whole MeshPiece: two pieces differing only in a field nothing reads are the
// same surface as far as anything downstream is concerned, and treating them as different means a
// rebuild that buys nothing. Everything here is either drawn with, batched by, or positioned by.
struct SurfaceKey
{
	const void  *material;    // what it is drawn with -- and what a batch is keyed on
	const void  *baseTex;     // what it animates from
	unsigned int rangeOffset; // where its geometry lives
	unsigned int rangeCount;  // and how much of it there is
	int          blendMode;   // which pass draws it
	int          translation; // which palette it is drawn in
	float        alpha;
	// The shading a backend bakes into its vertices. A change here does not move the surface between
	// batches, but it does mean the vertices it already uploaded are wrong.
	float        colorR, colorG, colorB;
	float        softLight, fogDensity;
	unsigned int fogColor;
	int          fogMode;
	float        normX, normY, normZ;

	SurfaceKey()
		: material(0), baseTex(0), rangeOffset(0), rangeCount(0), blendMode(0), translation(0),
		  alpha(1.f), colorR(1.f), colorG(1.f), colorB(1.f), softLight(-1.f), fogDensity(0.f),
		  fogColor(0), fogMode(0), normX(0.f), normY(0.f), normZ(0.f) {}
};

// [rc4l] The three answers, ordered by how much work they cost.
//
// They are ordered because a caller wants "at least this much", and an enum whose values escalate
// lets that be a comparison rather than a switch that has to enumerate every case correctly.
enum SurfaceChange
{
	kSurfaceUnchanged = 0,
	// The vertices this surface already uploaded are wrong, but it still belongs where it is. A
	// backend can rewrite them in place.
	kSurfaceRepaint   = 1,
	// It no longer belongs where it is -- a different material or pass means a different batch, and
	// moving a surface between batches is not something a patch can express.
	kSurfaceRebatch   = 2,
};

// Compare what a surface was against what it now is.
SurfaceChange ComputeSurfaceChange(const SurfaceKey &was, const SurfaceKey &now);

}} // namespace zx::surfaces

#endif
