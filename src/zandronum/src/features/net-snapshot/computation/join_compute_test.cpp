//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: join_compute_test.cpp
//
// Description: Tests for join_compute: the map stamp's distance through the
// wrap at 256, and the extent's floor and ceiling on whole, fractional and
// extreme values.
//
//-----------------------------------------------------------------------------

#include "join_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] Each case names itself through the assertion's own message rather than
// SCOPED_TRACE, whose thread-local trace stack faults inside GoogleTest on this platform.

// [Kaori-Chani] The joined map, a map behind it and one ahead, through the wrap, and the halves'
// edges.
TEST(SnapshotJoin, MapStampDistanceReadsThroughTheWrap)
{
	static const struct { BYTE stamp, joined; int want; } cases[] =
	{
		{ 7, 7, 0 }, { 6, 7, -1 }, { 8, 7, 1 },
		{ 0, 255, 1 }, { 255, 0, -1 },			// across the wrap
		{ 127, 0, 127 }, { 128, 0, -128 },		// the halves' edges
		{ 200, 72, -128 }, { 199, 72, 127 },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want, ComputeMapStampAhead(cases[i].stamp, cases[i].joined))
			<< "case " << i << ": stamp " << (int)cases[i].stamp
			<< ", joined " << (int)cases[i].joined;
	}
}

// [Kaori-Chani] A whole unit is its own floor and ceiling, a fraction rounds outward, and the
// fixed_t limits give whole units: on 48.16 that range is the 48 integer bits, so plus or minus
// 2^47 rather than the 32-bit build's plus or minus 32,768.
TEST(SnapshotJoin, ExtentTakesTheFloorAndCeilingOfOneAxis)
{
	static const struct { fixed_t v; long long fl, ce; } cases[] =
	{
		{ 0, 0, 0 },
		{ 5 * FRACUNIT, 5, 5 },
		{ 5 * FRACUNIT + 1, 5, 6 },
		{ -5 * FRACUNIT, -5, -5 },
		{ -5 * FRACUNIT - 1, -6, -5 },
		{ FRACUNIT / 2, 0, 1 },
		{ -FRACUNIT / 2, -1, 0 },
		{ FIXED_MAX, (1LL << 47) - 1, 1LL << 47 },
		{ FIXED_MIN, -(1LL << 47), -(1LL << 47) },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		long long lo[3] = { 1LL << 62, 1LL << 62, 1LL << 62 };
		long long hi[3] = { -(1LL << 62), -(1LL << 62), -(1LL << 62) };
		ComputeExtentTake(lo, hi, 1, cases[i].v);
		EXPECT_EQ(cases[i].fl, lo[1]) << "case " << i << ": raw " << (long long)cases[i].v;
		EXPECT_EQ(cases[i].ce, hi[1]) << "case " << i << ": raw " << (long long)cases[i].v;
		// [Kaori-Chani] The other two axes are left exactly as they came in.
		EXPECT_EQ(1LL << 62, lo[0]) << "case " << i;
		EXPECT_EQ(-(1LL << 62), hi[2]) << "case " << i;
	}
}

// [Kaori-Chani] A value inside the extent leaves it, and one past either end moves that end alone.
TEST(SnapshotJoin, ExtentOnlyWidens)
{
	long long lo[3] = { -10, -10, -10 }, hi[3] = { 10, 10, 10 };

	ComputeExtentTake(lo, hi, 0, 3 * FRACUNIT);
	EXPECT_EQ(-10, lo[0]);
	EXPECT_EQ(10, hi[0]);

	ComputeExtentTake(lo, hi, 0, 20 * FRACUNIT);
	EXPECT_EQ(-10, lo[0]);
	EXPECT_EQ(20, hi[0]);

	ComputeExtentTake(lo, hi, 2, -30 * FRACUNIT);
	EXPECT_EQ(-30, lo[2]);
	EXPECT_EQ(10, hi[2]);
}
