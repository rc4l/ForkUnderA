// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/walkslices_compute.h"

#include <gtest/gtest.h>

#include <random>

using namespace zx::visibility;

namespace {

// [rc4l] A wall as the clipper meets it: its span from start to end, rising and wrapping, and whether it hides what lies behind it.
struct Wall { unsigned start, end; bool solid; };

// [rc4l] Walk walls front to back through one clipper, as AddLine does: which were seen.
std::vector<bool> WalkWalls(ClipRanges &clip, const std::vector<Wall> &walls)
{
	std::vector<bool> seen(walls.size(), false);
	for (size_t i = 0; i < walls.size(); i++)
	{
		if (!clip.SafeCheckRange(walls[i].start, walls[i].end)) continue;
		seen[i] = true;
		if (walls[i].solid) clip.SafeAddClipRange(walls[i].start, walls[i].end);
	}
	return seen;
}

} // namespace

namespace {

// [rc4l] Edges splitting the view into `count` even slices.
std::vector<double> EvenEdges(int count)
{
	std::vector<double> e(count + 1);
	for (int k = 0; k <= count; k++) e[k] = double(k) / count;
	return e;
}

} // namespace

TEST(WalkSlices, AWholeTurnSplitsEvenlyFromZero)
{
	unsigned b[5];
	ComputeWalkSlices(0x12345678u, 0x80000000u, 4, &EvenEdges(4)[0], b);
	EXPECT_EQ(0u, b[0]);
	EXPECT_EQ(0x40000000u, b[1]);
	EXPECT_EQ(0x80000000u, b[2]);
	EXPECT_EQ(0xC0000000u, b[3]);
	EXPECT_EQ(0u, b[4]);
}

TEST(WalkSlices, AFrustumSplitsWhatItLeavesOpen)
{
	const unsigned view = 0x10000000u, half = 0x20000000u;
	unsigned b[9];
	ComputeWalkSlices(view, half, 8, &EvenEdges(8)[0], b);
	EXPECT_EQ(ComputeAngleToPseudo(view - half), b[0]);
	EXPECT_EQ(ComputeAngleToPseudo(view + half), b[8]);
	// [rc4l] Each slice rises from the last, through the wrap at zero.
	for (int k = 0; k < 8; k++) EXPECT_LT(b[k + 1] - b[0], 0x80000000u) << k;
	for (int k = 1; k < 8; k++) EXPECT_GT(b[k + 1] - b[0], b[k] - b[0]) << k;
}

TEST(WalkSlices, ASeedLeavesOnlyItsSliceOpen)
{
	ClipRanges clip;
	ComputeSliceSeed(clip, 0x40000000u, 0x50000000u);
	EXPECT_TRUE(clip.SafeCheckRange(0x44000000u, 0x45000000u));
	EXPECT_FALSE(clip.SafeCheckRange(0x51000000u, 0x52000000u));
	EXPECT_FALSE(clip.SafeCheckRange(0x10000000u, 0x20000000u));

	ClipRanges wrap;
	ComputeSliceSeed(wrap, 0xF0000000u, 0x10000000u);
	EXPECT_TRUE(wrap.SafeCheckRange(0xF8000000u, 0x08000000u));
	EXPECT_FALSE(wrap.SafeCheckRange(0x20000000u, 0x30000000u));

	ClipRanges none;
	ComputeSliceSeed(none, 0x40000000u, 0x40000000u);
	EXPECT_FALSE(none.SafeCheckRange(0x40000000u, 0x40000001u));
	EXPECT_FALSE(none.SafeCheckRange(0x10000000u, 0x90000000u));
}

TEST(WalkSlices, SlicesSeeExactlyWhatOneWalkSees)
{
	std::mt19937 rng(1234);
	for (int trial = 0; trial < 300; trial++)
	{
		const unsigned view = rng(), half = 0x18000000u + (rng() % 0x10000000u);
		std::vector<Wall> walls;
		for (int i = 0; i < 400; i++)
		{
			const unsigned start = ComputeAngleToPseudo(view - half - 0x04000000u + rng() % (2 * half + 0x08000000u));
			const unsigned span = 1 + rng() % (1u << (12 + rng() % 16));
			walls.push_back({ start, start + span, rng() % 10 < 7 });
		}
		ClipRanges whole;
		ComputeSeedFrustum(whole, view, half);
		const std::vector<bool> one = WalkWalls(whole, walls);

		const int count = 2 + trial % 15;
		std::vector<unsigned> b(count + 1);
		ComputeWalkSlices(view, half, count, &EvenEdges(count)[0], &b[0]);
		std::vector<bool> any(walls.size(), false);
		for (int k = 0; k < count; k++)
		{
			ClipRanges slice;
			ComputeSeedFrustum(slice, view, half);
			ComputeSliceSeed(slice, b[k], b[k + 1]);
			const std::vector<bool> seen = WalkWalls(slice, walls);
			for (size_t i = 0; i < walls.size(); i++) if (seen[i]) any[i] = true;
		}
		ASSERT_EQ(one, any) << "trial " << trial;
	}
}

TEST(WalkSlices, LeafCountsSumEachSubtreeFromTheRoot)
{
	std::vector<int> leaves;
	ComputeLeafCounts(NULL, 0, leaves);
	EXPECT_TRUE(leaves.empty());

	// [rc4l] Root 3; node 1 holds node 2, whose index is higher than its parent's.
	const CoverageNode nodes[4] = { { { ~3, ~4 } }, { { 2, ~1 } }, { { 0, ~2 } }, { { 1, ~0 } } };
	ComputeLeafCounts(nodes, 4, leaves);
	ASSERT_EQ(4u, leaves.size());
	EXPECT_EQ(2, leaves[0]);
	EXPECT_EQ(4, leaves[1]);
	EXPECT_EQ(3, leaves[2]);
	EXPECT_EQ(5, leaves[3]);
}

TEST(WalkSlices, LeakySegsAreThoseWithAnEndPastABoxAbove)
{
	// [rc4l] Root node 1: child 0 is node 0, child 1 subsector 2; node 0 holds subsectors 0 and 1.
	const int nodeUp[2] = { 2, -1 }, subUp[3] = { 0, 1, 3 };
	const int subFirst[3] = { 0, 1, 3 }, subCount[3] = { 1, 2, 0 };
	const long long points[12] = {
		0, 0, 4, 10,    // [rc4l] seg 0, inside every box above it
		4, 0, 8, 11,    // [rc4l] seg 1, its top end past node 0's child 1 and the root's child 0
		3, 5, 6, 5 };   // [rc4l] seg 2, past node 0's child 1 on the left only
	const long long boxes[16] = {
		10, 0, 0, 4,    10, 0, 4, 8,
		10, 0, 0, 8,    20, 10, 0, 8 };
	std::vector<int> start, list;
	ComputeLeakySegs(nodeUp, subUp, 3, subFirst, subCount, points, boxes, 4, start, list);
	ASSERT_EQ(5u, start.size());
	EXPECT_EQ(0, start[0]);
	EXPECT_EQ(0, start[1]);
	EXPECT_EQ(2, start[2]);
	EXPECT_EQ(3, start[3]);
	EXPECT_EQ(3, start[4]);
	ASSERT_EQ(3u, list.size());
	EXPECT_EQ(1, list[0]);
	EXPECT_EQ(2, list[1]);
	EXPECT_EQ(1, list[2]);

	ComputeLeakySegs(NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, start, list);
	ASSERT_EQ(1u, start.size());
	EXPECT_TRUE(list.empty());
}

TEST(WalkSlices, LeakForcesMatchOnlyWhenForcedIsExactlyWhatWasEntered)
{
	// [rc4l] Root node 1: child 0 is node 0, child 1 subsector 2; node 0 holds subsectors 0 and 1.
	const int nodeUp[2] = { 2, -1 }, subUp[3] = { 0, 1, 3 };
	std::vector<int> next;
	EXPECT_TRUE(ComputeLeakForces(std::vector<LeakBox>(), nodeUp, subUp, next));
	EXPECT_TRUE(next.empty());

	// [rc4l] Passed over at the root's child 0 though subsector 1 under it was reached: the path down to it is forced, once.
	std::vector<LeakBox> boxes;
	const LeakBox missed = { 2, 1, false, true }, alone = { 3, 2, false, false };
	boxes.push_back(missed);
	boxes.push_back(alone);
	boxes.push_back(missed);
	EXPECT_FALSE(ComputeLeakForces(boxes, nodeUp, subUp, next));
	ASSERT_EQ(2u, next.size());
	EXPECT_EQ(1, next[0]);
	EXPECT_EQ(2, next[1]);

	// [rc4l] Passed over right above the subsector: only that child.
	boxes.clear();
	const LeakBox near = { 1, 1, false, true };
	boxes.push_back(near);
	EXPECT_FALSE(ComputeLeakForces(boxes, nodeUp, subUp, next));
	ASSERT_EQ(1u, next.size());
	EXPECT_EQ(1, next[0]);

	// [rc4l] A child not above the subsector at all forces the whole way to the root.
	boxes.clear();
	const LeakBox astray = { 3, 0, false, true };
	boxes.push_back(astray);
	EXPECT_FALSE(ComputeLeakForces(boxes, nodeUp, subUp, next));
	ASSERT_EQ(2u, next.size());
	EXPECT_EQ(0, next[0]);
	EXPECT_EQ(2, next[1]);

	// [rc4l] Forced and entered elsewhere matches; forced with no slice entering it does not, and is dropped.
	boxes.clear();
	const LeakBox held = { 9, -1, true, true }, wrong = { 4, -1, true, false };
	boxes.push_back(held);
	boxes.push_back(alone);
	EXPECT_TRUE(ComputeLeakForces(boxes, nodeUp, subUp, next));
	ASSERT_EQ(1u, next.size());
	EXPECT_EQ(9, next[0]);
	boxes.push_back(wrong);
	EXPECT_FALSE(ComputeLeakForces(boxes, nodeUp, subUp, next));
	ASSERT_EQ(1u, next.size());
	EXPECT_EQ(9, next[0]);
}

TEST(WalkSlices, UnevenEdgesSplitTheViewWhereTheySay)
{
	const double edges[3] = { 0, 0.25, 1 };
	unsigned b[3];
	ComputeWalkSlices(0, 0x80000000u, 2, edges, b);
	EXPECT_EQ(0u, b[0]);
	EXPECT_EQ(0x40000000u, b[1]);
	EXPECT_EQ(0u, b[2]);
}

TEST(WalkSlices, BalancingMovesEdgesHalfwayToEqualWork)
{
	const double even[5] = { 0, 0.25, 0.5, 0.75, 1 };
	double out[5];
	// [rc4l] Equal work leaves them where they are.
	const long long same[4] = { 10, 10, 10, 10 };
	ComputeBalancedSlices(even, same, 4, 0.01, out);
	for (int k = 0; k <= 4; k++) EXPECT_DOUBLE_EQ(even[k], out[k]) << k;

	// [rc4l] All the work in the first quarter: equal shares would cut it at 1/16, 2/16 and 3/16, and the edges go halfway there.
	const long long first[4] = { 40, 0, 0, 0 };
	ComputeBalancedSlices(even, first, 4, 0.01, out);
	EXPECT_DOUBLE_EQ(0.5 * (0.25 + 0.0625), out[1]);
	EXPECT_DOUBLE_EQ(0.5 * (0.5 + 0.125), out[2]);
	EXPECT_DOUBLE_EQ(0.5 * (0.75 + 0.1875), out[3]);

	// [rc4l] No work measured leaves them too.
	const long long none[4] = { 0, 0, 0, 0 };
	ComputeBalancedSlices(even, none, 4, 0.01, out);
	EXPECT_DOUBLE_EQ(0.5, out[2]);
}

TEST(WalkSlices, BalancingKeepsEverySliceSomeWidth)
{
	const long long none[3] = { 0, 0, 0 };
	double out[4];
	const double crowded[4] = { 0, 0.001, 0.002, 1 };
	ComputeBalancedSlices(crowded, none, 3, 0.05, out);
	EXPECT_DOUBLE_EQ(0.05, out[1]);
	EXPECT_DOUBLE_EQ(0.1, out[2]);

	const double late[4] = { 0, 0.998, 0.999, 1 };
	ComputeBalancedSlices(late, none, 3, 0.05, out);
	EXPECT_DOUBLE_EQ(0.95, out[2]);
	EXPECT_DOUBLE_EQ(0.9, out[1]);
}
