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
	int      blend;         // 0 opaque/alpha-tested, 1 translucent, 2 additive, 3 fuzz
	bool     decal;         // paint on a surface: coplanar with it, and depth-biased to win against it
	unsigned first;         // vertex offset, which is creation order: older marks have smaller ones
};


// Strict weak ordering: true when `a` must be drawn BEFORE `b`.
//
// Decals first, as a stage, because that is what they are -- paint on the surfaces the world pass
// just drew, finished before anything standing in front of them. Within the stage: additive last,
// then oldest first, which is the sidedef attachment order GL walks. Everything else back to front.
//
// The distances of two coplanar quads differ only by where each centre falls, so ordering marks
// against each other by distance answers a question the numbers cannot actually settle.
bool ComputeDrawsBefore(const TranslucentDraw &a, const TranslucentDraw &b);

} }   // namespace zx::hwrender

#endif
