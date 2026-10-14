// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A walk split by view angle: each slice's clipper is open only over its share of the view, and each subsector is numbered by its place in the order one walk reaches them.

#ifndef ZX_WALKSLICES_COMPUTE_H
#define ZX_WALKSLICES_COMPUTE_H

#include "features/visibility/computation/clipranges_compute.h"
#include "features/visibility/computation/coverage_compute.h"

#include <vector>

namespace zx { namespace visibility {

// [rc4l] The pseudo-angles that split the view open past the frustum (`halfAngle` either side of `viewAngle`, the whole turn from half a turn up) into `count` slices at `edges`, fractions of that view from 0 to 1: bounds[k] to bounds[k + 1] is slice k, rising and wrapping.
void ComputeWalkSlices(unsigned viewAngle, unsigned halfAngle, int count, const double *edges, unsigned *bounds);

// [rc4l] Slice edges as fractions of the open view, moved halfway from `edges` to where each slice would get an equal share of the `work` the last walk measured in it, and kept `minGap` apart.
void ComputeBalancedSlices(const double *edges, const long long *work, int count, double minGap, double *out);

// [rc4l] Block everything but the open slice from `from` to `to`, as a slice's clipper starts out beside the frustum's own block.
void ComputeSliceSeed(ClipRanges &clip, unsigned from, unsigned to);

// [rc4l] Each node's subsector count, so a walk can number each subsector by its place in the front-to-back order from any eye: the front child's leaves come first.
void ComputeLeafCounts(const CoverageNode *nodes, int count, std::vector<int> &leaves);

// [rc4l] A box's sides as a node keeps them, raw 16.16.
enum { kBoxTop, kBoxBottom, kBoxLeft, kBoxRight };

// [rc4l] Per node child b (node * 2 + child, `children` of them) the segs under it with an end past its box (`boxes`, four per child), in `list` from start[b] to start[b + 1]: a node's box keeps whole units, so a vertex between them can lie outside it, where a slice that passes the box over may yet see the seg.
// [rc4l] Subsector i of `subs` holds segs subFirst[i] on, subCount[i] of them, each seg's two ends four numbers in `points`; subUp and nodeUp give each subsector's and node's node child, -1 at the root.
void ComputeLeakySegs(const int *nodeUp, const int *subUp, int subs, const int *subFirst, const int *subCount, const long long *points,
	const long long *boxes, int children, std::vector<int> &start, std::vector<int> &list);

// [rc4l] A node child a slice decided on alone: passed over though a seg under it in subsector `sub` showed in the slice, and whether some slice reached that subsector; or entered only because it was told to, and whether some slice entered it by its own test, as one walk then does.
struct LeakBox { int box, sub; bool forced, entered; };

// [rc4l] The node children to enter next time whatever the clipper says, sorted and once each: every one from a reached subsector up to the child passed over above it, and every forced one entered; true when the slices already matched one walk, none passing over what one walk reached and none forced into what it did not.
bool ComputeLeakForces(const std::vector<LeakBox> &boxes, const int *nodeUp, const int *subUp, std::vector<int> &next);

}} // namespace zx::visibility

#endif // ZX_WALKSLICES_COMPUTE_H
