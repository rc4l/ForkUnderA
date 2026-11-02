// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [Doruk-Aksoy] Tests for joinparts_compute: chunks gathered side by side join into the list one thread makes.

#include "features/jobs/computation/joinparts_compute.h"
#include "features/jobs/computation/jobpool_compute.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using zx::jobs::ComputeEmptyParts;
using zx::jobs::ComputeJoinParts;
using zx::jobs::Part;

namespace {

// [Doruk-Aksoy] Parts holding these items, in order.
std::vector<Part<int>> PartsOf(const std::vector<std::vector<int>> &lists)
{
	std::vector<Part<int>> parts(lists.size());
	for (size_t k = 0; k < lists.size(); k++) parts[k].items = lists[k];
	return parts;
}

} // namespace

// [Doruk-Aksoy] Parts join in their order whatever their sizes, empty ones included, and each start is where its part landed.
TEST(JoinParts, PartsJoinInOrderWithTheirStarts)
{
	std::vector<int> out = { 99 }, starts;
	ComputeJoinParts(PartsOf({ { 1, 2 }, {}, { 3 }, { 4, 5, 6 }, {} }), out, starts);
	EXPECT_EQ(std::vector<int>({ 1, 2, 3, 4, 5, 6 }), out);
	EXPECT_EQ(std::vector<int>({ 0, 2, 2, 3, 6 }), starts);
	ComputeJoinParts(std::vector<Part<int>>(), out, starts);
	EXPECT_TRUE(out.empty());
	EXPECT_TRUE(starts.empty());
}

// [Doruk-Aksoy] Neighbouring parts' ends lie two cache lines apart, so lanes growing them never share a line.
TEST(JoinParts, NeighbouringPartsLieTwoLinesApart)
{
	std::vector<Part<double>> parts;
	ComputeEmptyParts(parts, 3);
	EXPECT_EQ(sizeof(Part<double>), zx::jobs::kPartBytes);
	EXPECT_EQ((uintptr_t)&parts[1].items - (uintptr_t)&parts[0].items, zx::jobs::kPartBytes);
}

// [Doruk-Aksoy] A filter gathered by chunks across a pool, joined, is the filter one thread runs, on every pool size.
TEST(JoinParts, AFilterAcrossAPoolMatchesOneThread)
{
	std::vector<int> want;
	for (int i = 0; i < 10000; i++) if (i % 7 == 3 || i % 11 == 0) want.push_back(i);
	for (int workers = 0; workers <= 3; workers++)
	{
		zx::jobs::JobPool pool(workers, 0);
		std::vector<Part<int>> parts = PartsOf({ { -1 } });
		const int chunk = 333;
		ComputeEmptyParts(parts, zx::jobs::ComputeChunkCount(10000, chunk));
		auto gather = [&](int lo, int hi, int) { for (int i = lo; i < hi; i++) if (i % 7 == 3 || i % 11 == 0) parts[lo / chunk].items.push_back(i); };
		pool.ParallelChunks(10000, chunk, gather);
		std::vector<int> out, starts;
		ComputeJoinParts(parts, out, starts);
		EXPECT_EQ(want, out) << workers;
	}
	std::vector<Part<int>> parts = PartsOf({ { 1 } });
	ComputeEmptyParts(parts, -2);
	EXPECT_TRUE(parts.empty());
}
