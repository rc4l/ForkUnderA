// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which translucent draw goes down first.
//
// GL never sorts a decal against a sprite. Its renderer has fixed passes -- the opaque world, then
// each wall's decals drawn as passengers of the wall they are glued to, then one back-to-front list
// of translucent things -- so a decal's place comes from what it IS. The Diligent backend has one
// merged list and must reconstruct that from numbers, which is where every layering fault so far has
// come from: a scorch painted over its own glow, a scorch over the impact flash, a decal over a
// sprite.
//
// The reason those took so long is that the finished picture cannot say which of two draws landed
// second, so each attempt was aimed at a symptom. Here the inputs are stated and the answer is a
// bool, so a rule that never fires is visible immediately rather than after a play session. The
// "additive draws last" rule shipped requiring the two distances to be EXACTLY equal, and coplanar
// quads with different centres never are: it had never once run, and looked exactly like a rule that
// was firing and losing.
//
// Header-pure and engine-free, so the ordering is unit-tested off-engine and the backend stays thin
// glue around it.

#ifndef ZX_DECALORDER_COMPUTE_H
#define ZX_DECALORDER_COMPUTE_H

namespace zx { namespace hwrender {

// One entry in the translucent pass, reduced to what the order actually depends on.
struct TranslucentDraw
{
	float    distSq;        // squared distance from the camera to this draw's centre
	float    cx, cy, cz;    // that centre, so two marks on ONE spot can be told from two on two walls
	int      blend;         // 0 opaque/alpha-tested, 1 translucent, 2 additive, 3 fuzz
	bool     decal;         // paint on a surface: coplanar with it, and depth-biased to win against it
	unsigned first;         // vertex offset, which is creation order: older marks have smaller ones
};

// Two decals nearer than this are treated as marks on ONE spot.
//
// Inside it they overlap and their order is visible; beyond it they cannot overlap and their order
// cannot matter. A decal is at most 64 units across, so that is the radius.
extern const float kCoincidentDecalRadius;

// A decal sorts as very slightly FARTHER than it is.
//
// It is paint on a surface, so anything standing in front of that surface must be drawn over it. A
// fire sprite hovering a few units above the floor it is scorching sits at almost the same distance
// as the mark, and a plain farthest-first sort then lands the decal second and buries the sprite.
// Proportional, so it only ever decides a near-coincident pair and never reorders anything genuinely
// in front or behind.
extern const float kDecalDistanceNudge;

// Apply that nudge. Callers must use this when FILLING the list, not when comparing: the value has
// to be in the field the comparator reads, and a version that multiplied one line above the
// assignment that overwrote it meant the rule had never applied at all.
float ComputeSortDistance(float distSq, bool decal);

// Strict weak ordering: true when `a` must be drawn BEFORE `b`.
//
// Farthest first, except that two marks on one spot are ordered by what they are -- additive last,
// because additive blending only brightens and so can only be lost by being buried, then oldest
// first so a fresh scorch covers an old one.
bool ComputeDrawsBefore(const TranslucentDraw &a, const TranslucentDraw &b);

} }   // namespace zx::hwrender

#endif
