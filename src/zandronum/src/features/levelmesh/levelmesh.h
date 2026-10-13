// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Persistent level mesh (features/levelmesh) -- phase P0: measurement only.
//
// Nothing renders from this yet. P0's whole job is to answer the affordability question the plan
// opens with: if every sidedef gets a FIXED vertex range, what does a real map cost? See
// docs/levelmesh-PLAN.md.

#ifndef ZX_LEVELMESH_H
#define ZX_LEVELMESH_H

#include "features/levelmesh/computation/surfacebudget_compute.h"
#include "tarray.h"
#include "basictypes.h"   // [rc4l] fixed_t

struct sector_t;

namespace zx { namespace levelmesh {

// Vertex stride the mesh would use, matching FFlatVertexBuffer's FFlatVertex (3 position floats +
// 2 texcoord floats). Walls need the same attributes, so the wall buffer would share the layout.
const int kVertexStrideBytes = 5 * 4;

// Walk the loaded level and collect the static budget inputs, one per sidedef. Empty if no level is
// loaded. Requires gl_PreprocessLevel to have run: side_t::numsegs comes from PrepareSegs and
// vertex_t::numsectors from InitVertexData, so on a dedicated server (which skips PreprocessLevel)
// both read zero and the result understates -- CollectSideBudgets reports that via `preprocessed`.
struct Collected
{
	TArray<SideBudgetInput> sides;
	bool preprocessed;   // false when the GL level data was never built (server, or pre-load)
	long long flatVertices; // what FFlatVertexBuffer already reserves, for a like-for-like total

	// [rc4l] How much GLWall::SplitWall actually fires. Its per-band splitting is the only thing
	// making a side's vertex count vary at runtime (P_Recalculate3DFloors rebuilds lightlist every
	// frame for moving sectors), so it gates the fixed-range design -- but only for the sides it
	// touches. If those are rare, P2a's shader work is not on the critical path and P2b can reserve
	// a bounded band count and leave the rest on the old path.
	int sectorsWithFFloors;   // sectors carrying any 3D floor
	int sectorsWithBands;     // sectors whose lightlist actually splits walls (size > 1)
	int maxLightlistSize;     // deepest band stack on the map
	int sidesTouchingBands;   // sides whose front sector splits, i.e. what P2a would have to cover
};

void CollectSideBudgets(Collected &out);

// [rc4l] The lower edge at (x, y) of a sector's 3D floor light list entry: the next entry's plane, or the sector's floor under the last.
fixed_t LightListBottom(const sector_t *sector, unsigned entry, fixed_t x, fixed_t y);

}} // namespace zx::levelmesh

#endif // ZX_LEVELMESH_H
