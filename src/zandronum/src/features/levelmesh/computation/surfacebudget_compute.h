// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Worst-case vertex budgeting for the persistent level mesh (features/levelmesh).
//
// The mesh gives every sidedef a FIXED vertex range assigned once at build and never moved, the way
// FFlatVertexBuffer already does for sector planes. A range can only be fixed if its size can be
// bounded from facts that never change at runtime, so this unit derives that bound. It is the
// measurement that decides whether the fixed-range design is affordable at all -- see
// docs/levelmesh-PLAN.md, "Vertex budgeting still needs measurement".
//
// Everything here is static after P_SetupLevel: level topology is immutable at runtime (lines,
// sides, sectors and segs are never added or removed), so seg counts and per-vertex sector counts
// are load-time constants. Only positions, materials and transforms move.
//
// Header-pure and engine-free so it is unit-tested off-engine; levelmesh.cpp does the walking.

#ifndef ZX_SURFACEBUDGET_COMPUTE_H
#define ZX_SURFACEBUDGET_COMPUTE_H

namespace zx { namespace levelmesh {

// The static facts one sidedef contributes to its own budget.
struct SideBudgetInput
{
	int  numSegs;            // segs on this sidedef; >= 1 for any side the BSP kept
	int  leftVertexSectors;  // vertex_t::numsectors at the side's first vertex
	int  rightVertexSectors; // vertex_t::numsectors at the side's second vertex
	int  ffloorBlocks;       // 3D-floor blocks that can render on this side (front + back rovers)
	bool twoSided;           // a back sector exists -> upper/mid/lower rather than one mid
};

// Vertices in one wall piece (one quad plus its welded T-junction vertices).
//
// Four corners, plus the seamless-edge splits that gl_vertex.cpp inserts per frame today and the
// mesh bakes once. A shared vertex contributes at most one split per distinct plane height meeting
// there, and each sector touching it offers two (floor + ceiling) -- that is exactly the sizing
// gl_RecalcVertexHeights uses when it allocates heightlist as numsectors*2. The upper and lower
// edges each split at every *other* seg on the sidedef, hence numSegs-1 apiece.
//
// Note this bound assumes the equal-height dedupe in gl_RecalcVertexHeights is dropped (plan P2b):
// with the dedupe the count varies as sectors move, which is precisely what a fixed range forbids.
int ComputePieceVertexBudget(int numSegs, int leftVertexSectors, int rightVertexSectors);

// Wall pieces a sidedef can present at once.
//
// Three for a two-sided line (upper, mid, lower) and one for a one-sided line, plus one per 3D-floor
// block. Pieces absent this frame -- a closed door has no upper -- collapse to zero-area triangles
// rather than shrinking the range, so the count is a constant, not a maximum-of-observed.
int ComputeSidePieceCount(bool twoSided, int ffloorBlocks);

// Total worst-case vertices reserved for one sidedef.
int ComputeSideVertexBudget(const SideBudgetInput &in);

// What a whole level costs. Reported by fua_levelmesh_stats so the design decision rests on real
// maps rather than on this header's arithmetic.
struct LevelBudget
{
	long long totalVertices; // summed worst case across every side
	int       sides;         // sides counted
	int       maxPerSide;    // largest single side, the tail that decides pooling-vs-fixed
	int       worstSideIndex;// which side that was, for eyeballing it in an editor; -1 if none
};

LevelBudget ComputeLevelBudget(const SideBudgetInput *sides, int count);

// Bytes a vertex buffer of that many vertices occupies, given the vertex stride. Split out so the
// stats command and any future allocator agree on one arithmetic.
long long ComputeBufferBytes(long long vertices, int strideBytes);

}} // namespace zx::levelmesh

#endif // ZX_SURFACEBUDGET_COMPUTE_H
