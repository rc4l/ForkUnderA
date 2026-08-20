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

#ifndef ZX_SURFACEORDER_COMPUTE_H
#define ZX_SURFACEORDER_COMPUTE_H

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

// [rc4l] The BUILD-time order: which piece's vertices go into the buffer before which.
//
// A different question from the one above, and it was answered by a lambda inside BuildSceneBuffer
// with three rules stacked in it -- opaque before blended, then material, then base texture. Two of
// those are batching (put identical state together so it draws once); one is CORRECTNESS, because a
// translucent surface has to be drawn after everything it shows through and cannot be sorted into
// the middle of the opaque run. Mixing the two in one unlabelled comparator is how the correctness
// half gets "tidied up" later by someone reading it all as batching.
//
// Materials and textures compare as opaque handles: the values mean nothing, they only have to
// order consistently, which is what puts equal state next to itself.
struct ScenePiece
{
	int         blendMode;    // 0 opaque/alpha-tested, non-zero blended
	const void *material;
	const void *baseTex;      // the animation identity: two frames of one flat share a material
};

bool ComputePiecesBefore(const ScenePiece &a, const ScenePiece &b);

// [rc4l] And GL's own rule for sprites, which is NOT the rule above.
//
// GL sorts translucent sprites by depth descending, breaking ties by spawn index -- forwards or
// backwards depending on COMPATF_SPRITESORT, because maps exist that depend on the older behaviour.
// The port sorts everything translucent together by distance, which is a different answer wherever
// the two disagree.
//
// Both live here deliberately. One authority does not mean every renderer answers the same way --
// GL is the oracle and its answer is the shipped one -- it means the answers are written down where
// they can be compared, instead of three lines inside two comparators nobody reads together.
struct GLSpriteOrder
{
	int depth;        // GL's own depth measure for the sprite
	int spawnIndex;   // GLSprite::index, the order it entered the list
};

bool ComputeGLSpritesBefore(const GLSpriteOrder &a, const GLSpriteOrder &b, bool compatSpriteSort);

} }   // namespace zx::hwrender

#endif
