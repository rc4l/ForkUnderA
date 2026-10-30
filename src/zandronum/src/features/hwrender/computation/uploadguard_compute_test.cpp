// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "uploadguard_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

// [rc4l] The light grid and the 2D layer were sent every frame, the same bytes while the view stood still.
TEST(UploadGuard, TheSameBytesToTheSameBufferAreNotSentAgain)
{
	UploadShadow s;
	const unsigned a[3] = { 1, 2, 3 };
	EXPECT_TRUE(ComputeUploadChanged(s, 7, a, sizeof(a)));
	EXPECT_FALSE(ComputeUploadChanged(s, 7, a, sizeof(a)));
	const unsigned b[3] = { 1, 2, 4 };
	EXPECT_TRUE(ComputeUploadChanged(s, 7, b, sizeof(b)));
	EXPECT_FALSE(ComputeUploadChanged(s, 7, b, sizeof(b)));
}

TEST(UploadGuard, ABufferMadeAgainOrAnotherSizeIsAlwaysSent)
{
	UploadShadow s;
	const unsigned a[3] = { 1, 2, 3 };
	EXPECT_TRUE(ComputeUploadChanged(s, 7, a, sizeof(a)));
	EXPECT_TRUE(ComputeUploadChanged(s, 8, a, sizeof(a)));
	EXPECT_TRUE(ComputeUploadChanged(s, 8, a, sizeof(unsigned) * 2));
	EXPECT_FALSE(ComputeUploadChanged(s, 8, a, sizeof(unsigned) * 2));
}

TEST(UploadGuard, NothingSentTwiceIsUnchanged)
{
	UploadShadow s;
	EXPECT_TRUE(ComputeUploadChanged(s, 3, nullptr, 0));
	EXPECT_FALSE(ComputeUploadChanged(s, 3, nullptr, 0));
}

// [rc4l] The sector light table sends only the sectors whose light changed, neighbours a few apart in one write.
TEST(ComputeChangedRuns, OnlyTheChangedEntriesNearbyOnesJoined)
{
	unsigned was[20] = {}, now[20] = {};
	EXPECT_TRUE(ComputeChangedRuns(was, now, 10, 2, 2).empty());
	now[2] = 1;          // [rc4l] entry 1
	now[7] = 1;          // [rc4l] entry 3, one unchanged entry after the first run, so joined
	now[18] = 5;         // [rc4l] entry 9, far off
	const std::vector<UploadRun> runs = ComputeChangedRuns(was, now, 10, 2, 2);
	ASSERT_EQ(2u, runs.size());
	EXPECT_EQ(1u, runs[0].first);
	EXPECT_EQ(3u, runs[0].count);
	EXPECT_EQ(9u, runs[1].first);
	EXPECT_EQ(1u, runs[1].count);
	// [rc4l] With no bridge each changed entry is its own run.
	EXPECT_EQ(3u, ComputeChangedRuns(was, now, 10, 2, 0).size());
}

// [Doruk-Aksoy] The sector light table is read again only after a fill or a level change, or when it is not held.
TEST(ComputeCopyTableStale, ReadAgainOnlyWhenTheCopyOrLevelChanged)
{
	int a = 0, b = 0;
	CopyTableKey was;
	was.fills = 7; was.sectors = &a; was.count = 10;
	CopyTableKey now = was;
	EXPECT_FALSE(ComputeCopyTableStale(was, now, true));
	EXPECT_TRUE(ComputeCopyTableStale(was, now, false)) << "a table not held is always read";
	now.fills = 8;
	EXPECT_TRUE(ComputeCopyTableStale(was, now, true));
	now = was; now.sectors = &b;
	EXPECT_TRUE(ComputeCopyTableStale(was, now, true));
	now = was; now.count = 11;
	EXPECT_TRUE(ComputeCopyTableStale(was, now, true));
	EXPECT_TRUE(ComputeCopyTableStale(CopyTableKey(), was, true)) << "a table never made from the copy is read";
}
