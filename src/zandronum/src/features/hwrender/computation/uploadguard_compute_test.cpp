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

// [Doruk-Aksoy] A moved view's grid is always sent with nothing kept, so the frame after it, though its bytes match the old shadow's, is sent again, and only then kept.
TEST(UploadGuard, ChangingBytesAreSentAndForgottenSoNoLaterFrameMatchesThem)
{
	UploadShadow s;
	const unsigned a[3] = { 1, 2, 3 }, b[3] = { 4, 5, 6 };
	EXPECT_TRUE(ComputeUploadDue(s, 7, a, sizeof(a), false));
	EXPECT_FALSE(ComputeUploadDue(s, 7, a, sizeof(a), false));
	EXPECT_TRUE(ComputeUploadDue(s, 7, b, sizeof(b), true));
	EXPECT_TRUE(s.bytes.empty());
	EXPECT_TRUE(ComputeUploadDue(s, 7, a, sizeof(a), false));
	EXPECT_FALSE(ComputeUploadDue(s, 7, a, sizeof(a), false));
}

// [Doruk-Aksoy] Kept floats say whether they changed and hold the new ones after.
TEST(UploadGuard, KeptFloatsSayWhenTheyChanged)
{
	float last[3] = { 0.f, 0.f, 0.f };
	const float now[3] = { 1.f, 2.f, 3.f };
	EXPECT_TRUE(ComputeKeptDiffers(last, now, 3));
	EXPECT_EQ(2.f, last[1]);
	EXPECT_FALSE(ComputeKeptDiffers(last, now, 3));
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

// [Doruk-Aksoy] The sector light table reads only the sectors the fills took, and every sector after a level change, when it is not held, or when the taken sectors are not known.
TEST(ComputeCopyTableWhole, EverySectorOnlyWhenTheTakenOnesAreNotEnough)
{
	int a = 0, b = 0;
	CopyTableKey was;
	was.sectors = &a; was.count = 10;
	CopyTableKey now = was;
	EXPECT_FALSE(ComputeCopyTableWhole(was, now, true, true));
	EXPECT_TRUE(ComputeCopyTableWhole(was, now, false, true)) << "a table not held is always read whole";
	EXPECT_TRUE(ComputeCopyTableWhole(was, now, true, false)) << "a level's start took every sector without listing them";
	now.sectors = &b;
	EXPECT_TRUE(ComputeCopyTableWhole(was, now, true, true));
	now = was; now.count = 11;
	EXPECT_TRUE(ComputeCopyTableWhole(was, now, true, true));
	EXPECT_TRUE(ComputeCopyTableWhole(CopyTableKey(), was, true, true)) << "a table never made from the copy is read whole";
}
