// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/gamecopy/computation/changelog_compute.h"

#include <gtest/gtest.h>

using namespace zx::gamecopy;

// [Doruk-Aksoy] A level's log starts with nothing noted, or whole when what came before it is unknown.
TEST(ChangeLogCompute, AResetLogHoldsNothing)
{
	ChangeLog log;
	EXPECT_TRUE(log.whole) << "a log never reset knows nothing";
	ComputeLogReset(log, 4, false);
	EXPECT_FALSE(log.whole);
	EXPECT_EQ(log.noted.size(), 4u);
	EXPECT_TRUE(log.list.empty());
	ComputeLogReset(log, -3, true);
	EXPECT_TRUE(log.whole);
	EXPECT_TRUE(log.noted.empty());
}

// [Doruk-Aksoy] An item changed many times is compared once, in the order it first changed.
TEST(ChangeLogCompute, EachItemIsNotedOnce)
{
	ChangeLog log;
	ComputeLogReset(log, 8, false);
	for (int i : { 5, 2, 5, 7, 2 }) ComputeLogNote(log, i);
	EXPECT_EQ(log.list, (std::vector<int>{ 5, 2, 7 }));
	EXPECT_FALSE(log.whole);
}

// [Doruk-Aksoy] A note for an item the log does not hold, such as one made before the level's arrays, makes the next compare whole.
TEST(ChangeLogCompute, ANoteOutsideTheItemsMakesItWhole)
{
	ChangeLog log;
	ComputeLogReset(log, 3, false);
	ComputeLogNote(log, 3);
	EXPECT_TRUE(log.whole);
	ComputeLogReset(log, 3, false);
	ComputeLogNote(log, -1);
	EXPECT_TRUE(log.whole);
	EXPECT_TRUE(log.list.empty());
}

// [Doruk-Aksoy] Once taken, nothing is noted, so an item noted again is listed again.
TEST(ChangeLogCompute, ClearingForgetsWhatWasTaken)
{
	ChangeLog log;
	ComputeLogReset(log, 4, true);
	ComputeLogNote(log, 1);
	ComputeLogClear(log);
	EXPECT_FALSE(log.whole) << "a fill takes every change, so the log holds again";
	EXPECT_TRUE(log.list.empty());
	ComputeLogNote(log, 1);
	EXPECT_EQ(log.list, (std::vector<int>{ 1 }));
}

// [Doruk-Aksoy] Items no longer worth listing leave the log and can be noted again.
TEST(ChangeLogCompute, KeepIfDropsWhatItRefuses)
{
	ChangeLog log;
	ComputeLogReset(log, 6, false);
	for (int i : { 4, 1, 3, 0 }) ComputeLogNote(log, i);
	ComputeLogKeepIf(log, [](int i) { return i % 2 == 1; });
	EXPECT_EQ(log.list, (std::vector<int>{ 1, 3 }));
	EXPECT_EQ(log.noted[4], 0);
	EXPECT_EQ(log.noted[1], 1);
	ComputeLogNote(log, 4);
	EXPECT_EQ(log.list, (std::vector<int>{ 1, 3, 4 }));
}

// [Doruk-Aksoy] An item's address gives its index, and anything outside the items or between two of them gives none.
TEST(ChangeLogCompute, AnAddressFindsItsItem)
{
	const uintptr_t first = 0x1000;
	EXPECT_EQ(ComputeIndexIn(first, first, 16, 4), 0);
	EXPECT_EQ(ComputeIndexIn(first + 48, first, 16, 4), 3);
	EXPECT_EQ(ComputeIndexIn(first + 64, first, 16, 4), -1) << "one past the last";
	EXPECT_EQ(ComputeIndexIn(first - 16, first, 16, 4), -1) << "before the first";
	EXPECT_EQ(ComputeIndexIn(first + 8, first, 16, 4), -1) << "inside an item, not at one";
	EXPECT_EQ(ComputeIndexIn(first, 0, 16, 4), -1) << "no items allocated";
	EXPECT_EQ(ComputeIndexIn(first, first, 0, 4), -1);
	EXPECT_EQ(ComputeIndexIn(first, first, 16, 0), -1);
}

// [Doruk-Aksoy] Slices cover every item in turn and start over, the last one short.
TEST(ChangeLogCompute, SlicesWalkTheItemsAndWrap)
{
	int cursor = 0, lo = -1, hi = -1;
	ComputeVerifySlice(10, 4, cursor, lo, hi);
	EXPECT_EQ(lo, 0); EXPECT_EQ(hi, 4); EXPECT_EQ(cursor, 4);
	ComputeVerifySlice(10, 4, cursor, lo, hi);
	EXPECT_EQ(lo, 4); EXPECT_EQ(hi, 8);
	ComputeVerifySlice(10, 4, cursor, lo, hi);
	EXPECT_EQ(lo, 8); EXPECT_EQ(hi, 10);
	EXPECT_EQ(cursor, 0) << "the end starts the walk over";
	ComputeVerifySlice(10, 4, cursor, lo, hi);
	EXPECT_EQ(lo, 0); EXPECT_EQ(hi, 4);
}

// [Doruk-Aksoy] A cursor past a map that shrank starts over, and an empty map or slice compares nothing.
TEST(ChangeLogCompute, SlicesSurviveAChangedMap)
{
	int cursor = 50, lo = -1, hi = -1;
	ComputeVerifySlice(10, 4, cursor, lo, hi);
	EXPECT_EQ(lo, 0); EXPECT_EQ(hi, 4);
	cursor = -2;
	ComputeVerifySlice(10, 20, cursor, lo, hi);
	EXPECT_EQ(lo, 0); EXPECT_EQ(hi, 10); EXPECT_EQ(cursor, 0);
	cursor = 3;
	ComputeVerifySlice(0, 4, cursor, lo, hi);
	EXPECT_EQ(lo, 0); EXPECT_EQ(hi, 0); EXPECT_EQ(cursor, 0);
	cursor = 3;
	ComputeVerifySlice(10, 0, cursor, lo, hi);
	EXPECT_EQ(lo, hi);
}
