// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The wall faces a 3D floor puts on a sidedef.
//
// A 3D floor is a slab hanging in a sector, and the wall behind it is not one surface: each slab cuts
// a block out of it. GLWall::DoFFloorBlocks walks the BACK sector's slabs from the top down, clips
// each against what the previous one already covered, and emits a block per slab.
//
// This is that walk as a function over numbers. It is the last thing GL derives that the map-driven
// bake does not, and it is the whole of the 4.0% between the two pictures on dbab02: the faces are
// built during the BSP traversal, so when the traversal stops they are simply absent.
//
// Engine-free: heights in, spans out. No seg, no sector, no F3DFloor.

#ifndef ZX_FFBLOCKS_COMPUTE_H
#define ZX_FFBLOCKS_COMPUTE_H

namespace zx { namespace surfaces {

// One 3D floor, as this computation needs it: where its slab sits at the wall's two ends.
struct FFRover
{
	float top[2], bottom[2];
	// Whether it draws sides at all, and whether it draws them facing INWARD -- an inverted slab is
	// handled by the front sector's pass, not this one, and must be skipped here.
	bool  renderSides;
	bool  invertSides;
};

// One block of wall, cut out by one slab.
struct FFBlock
{
	float top[2], bottom[2];
	int   rover;   // which entry of the list produced it
};

// [rc4l] Walk the slabs top down and cut the wall into blocks.
//
// wallTop and wallBottom are the span the ordinary wall parts already agreed on, at both ends. The
// slabs are expected in the order the sector holds them, which is top to bottom -- the same order GL
// relies on, and the reason this is a single pass rather than a sort.
//
// Returns how many blocks were written. A slab entirely above the wall contributes nothing; one that
// overlaps the previous block is clipped to it; and the walk stops as soon as the running top reaches
// the wall's bottom, because everything below that is the wall's own lower part.
int ComputeFFBlocks(const float *wallTop, const float *wallBottom,
	const FFRover *rovers, int nRovers, FFBlock *out, int maxOut);

}} // namespace zx::surfaces

#endif // ZX_FFBLOCKS_COMPUTE_H
