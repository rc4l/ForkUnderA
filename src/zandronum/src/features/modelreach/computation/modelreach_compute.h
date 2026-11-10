// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_MODELREACH_COMPUTE_H
#define ZX_MODELREACH_COMPUTE_H

#include <vector>

namespace zx { namespace modelreach {

// [Doruk-Aksoy] How far a model's vertices lie from its origin in its own space (x, up, y): in all three axes, and across the two flat ones.
struct VertexReach
{
	float full;
	float flat;
};

// [Doruk-Aksoy] The farthest of count vertices, each `stride` floats apart starting with x, up, y.
VertexReach ComputeVertexReach(const float *xyz, int count, int stride);

// [Doruk-Aksoy] Where a MODELDEF frame puts its model, as ModelPose takes it in (x, up, y): what can only grow the model and never shrink it.
struct FramePlacement
{
	float scale[3];        // [Doruk-Aksoy] MODELDEF's scales
	float offset[3];       // [Doruk-Aksoy] MODELDEF's offsets in map units, which the actor's scale scales
	float stretch;         // [Doruk-Aksoy] the stretch of up, a voxel's aspect factor
	bool rotating;         // [Doruk-Aksoy] ROTATING, spun about spinCenter (map units the actor's scale leaves alone) and spinAxis
	float spinCenter[3];
	float spinAxis[3];
	bool tilts;            // [Doruk-Aksoy] a pitch or roll can turn the model's up toward the flat
};

// [Doruk-Aksoy] How far an actor class's models reach from its origin: the part its scale multiplies and the part it leaves alone, across the flat axes and in all three, at least `floor`, and nothing at all when `off`.
struct ModelReach
{
	float flatScaled, flatFixed;
	float fullScaled, fullFixed;
	float floor;
	bool off;
};

// [Doruk-Aksoy] Grows a class's reach to hold one frame's model placed as `p`.
void ComputeAddFrameReach(const VertexReach &v, const FramePlacement &p, ModelReach &into);

// [Doruk-Aksoy] DECORATE's RenderRadius on a class: below 0 the actor is drawn from its centre alone, above 0 its reach is at least that.
void ComputeApplyRenderRadius(double renderRadius, ModelReach &r);

// [Doruk-Aksoy] One actor's reach at its scale: across the flat axes (the footprint sectors are found in) and in all three (the frustum's pad).
void ComputeActorReach(const ModelReach &r, float scaleX, float scaleY, float &footprint, float &full);

// [Doruk-Aksoy] Whether an actor reaches past its radius, so the sectors its centre is in no longer say where it is seen from.
bool ComputeReachIsWide(float footprint, float radius);

// [Doruk-Aksoy] An axis-aligned box on the map, in map units.
struct Box
{
	double left, bottom, right, top;
};

// [Doruk-Aksoy] The square `reach` around an actor wherever it stands between its last tic's spot and this one's.
Box ComputeFootprintBox(double prevX, double prevY, double x, double y, double reach);

// [Doruk-Aksoy] Whether a line passes into a box, as P_CreateSecNodeList takes a line into a thing's sectors: its bounds overlap the box's and the box's corners are not all on one side of it.
bool ComputeBoxCrossesLine(const Box &b, double x1, double y1, double x2, double y2);

// [Doruk-Aksoy] The blockmap's lines in the cells a box covers, each once in ascending order; blockmap holds each cell's offset into lump, whose list for a cell starts after a 0 and ends at -1.
void ComputeBlockmapLines(const int *blockmap, const int *lump, int width, int height, double orgX, double orgY, double blockSize, const Box &b,
	std::vector<int> &out);

// [Doruk-Aksoy] A line as the footprint reads it: its ends and the sectors on either side, -1 for none.
struct FootLine
{
	double x1, y1, x2, y2;
	int front, back;
};

// [Doruk-Aksoy] The sectors an actor's box touches: its centre's, and both sides of every line that passes into the box, each once in ascending order.
void ComputeFootprintSectors(const FootLine *lines, int count, const Box &b, int centreSector, std::vector<int> &out);

// [Doruk-Aksoy] Whether any of the sectors carries this walk's stamp.
bool ComputeAnyReached(const int *sectors, int count, const unsigned *stamps, int stampCount, unsigned stamp);

}} // namespace zx::modelreach

#endif // ZX_MODELREACH_COMPUTE_H
