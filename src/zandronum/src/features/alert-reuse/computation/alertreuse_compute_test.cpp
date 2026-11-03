// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for skipping a noise alert that repeats a recent one from inside the area it reached.
#include "gtest/gtest.h"
#include "features/alert-reuse/computation/alertreuse_compute.h"

#include <initializer_list>

namespace
{
int g_playerA, g_playerB;
const void *const kPlayerA = &g_playerA;
const void *const kPlayerB = &g_playerB;
constexpr int kSectors = 8;
constexpr int kReuse = 10;

// [Doruk-Aksoy] A full alert by target at now that reached sectors unblocked.
void Alert(ZxAlertReuse &reuse, const void *target, int now, std::initializer_list<int> sectors, bool splash = false)
{
	ZxAlertMemo &memo = ComputeAlertBegin(reuse, target, splash, now, kSectors);
	for (int sector : sectors) ComputeAlertCovers(memo, sector);
}

// [Doruk-Aksoy] A repeat from a sector the last alert reached is skipped up to reuseTics later, and not after.
TEST(ComputeAlertRepeats, WithinTheTicsFromTheReachedArea)
{
	ZxAlertReuse reuse;
	Alert(reuse, kPlayerA, 100, { 1, 2, 3 });
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 100, kReuse));
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 3, 110, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 3, 111, kReuse));
}

// [Doruk-Aksoy] Leaving the reached area, another target, another splash, a maximum distance, the cvar off or time going back each run the alert in full.
TEST(ComputeAlertRepeats, EverythingElseRunsInFull)
{
	ZxAlertReuse reuse;
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 100, kReuse));
	Alert(reuse, kPlayerA, 100, { 1, 2 });
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 4, 101, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerB, false, false, 1, 101, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, true, false, 1, 101, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, true, 1, 101, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 101, 0));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 99, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, -1, 101, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, kSectors, 101, kReuse));
}

// [Doruk-Aksoy] Only the newest full alert's area counts, so a sector the older one reached no longer does.
TEST(ComputeAlertRepeats, OnlyTheNewestAlertsArea)
{
	ZxAlertReuse reuse;
	Alert(reuse, kPlayerA, 100, { 1, 2 });
	Alert(reuse, kPlayerA, 105, { 2, 3 });
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 106, kReuse));
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 3, 106, kReuse));
}

// [Doruk-Aksoy] Each target and splash keeps its own memo, so two players firing both skip their repeats.
TEST(ComputeAlertBegin, OneMemoPerTargetAndSplash)
{
	ZxAlertReuse reuse;
	Alert(reuse, kPlayerA, 100, { 1 });
	Alert(reuse, kPlayerB, 101, { 2 });
	Alert(reuse, kPlayerA, 102, { 3 }, true);
	Alert(reuse, kPlayerA, 103, { 1 });
	EXPECT_EQ(reuse.memos.size(), 3u);
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 104, kReuse));
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerB, false, false, 2, 104, kReuse));
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerA, true, false, 3, 104, kReuse));
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, true, false, 1, 104, kReuse));
}

// [Doruk-Aksoy] With every memo taken a new target replaces the oldest, whose alerts then run in full.
TEST(ComputeAlertBegin, ReplacesTheOldestWhenFull)
{
	ZxAlertReuse reuse;
	int targets[kZxAlertMemos + 1];
	for (int i = 0; i < kZxAlertMemos; i++) Alert(reuse, &targets[i], i == 3 ? 50 : 100 + i, { 1 });
	Alert(reuse, &targets[kZxAlertMemos], 200, { 1 });
	EXPECT_EQ((int)reuse.memos.size(), kZxAlertMemos);
	EXPECT_FALSE(ComputeAlertRepeats(reuse, &targets[3], false, false, 1, 55, kReuse));
	EXPECT_TRUE(ComputeAlertRepeats(reuse, &targets[kZxAlertMemos], false, false, 1, 201, kReuse));
	EXPECT_TRUE(ComputeAlertRepeats(reuse, &targets[4], false, false, 1, 105, kReuse));
}

// [Doruk-Aksoy] A level with another sector count starts the memo's area afresh, and a sector out of range is never marked.
TEST(ComputeAlertBegin, ANewSectorCountStartsAfresh)
{
	ZxAlertReuse reuse;
	Alert(reuse, kPlayerA, 100, { 1 });
	ZxAlertMemo &memo = ComputeAlertBegin(reuse, kPlayerA, false, 101, 4);
	ComputeAlertCovers(memo, 4);
	ComputeAlertCovers(memo, -1);
	EXPECT_EQ(memo.covered.size(), 4u);
	EXPECT_FALSE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 102, kReuse));
	ComputeAlertCovers(memo, 1);
	EXPECT_TRUE(ComputeAlertRepeats(reuse, kPlayerA, false, false, 1, 102, kReuse));
}
}
