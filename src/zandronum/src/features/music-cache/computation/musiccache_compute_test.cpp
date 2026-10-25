// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the music cache's budget and plan.
#include "gtest/gtest.h"
#include "features/music-cache/computation/musiccache_compute.h"

namespace
{
constexpr long long kMB = 1024 * 1024;

// [Doruk-Aksoy] A song's own name, or the part past "file://", is what its lump is found by.
TEST(ComputeMusicLumpName, NamesALump)
{
	const char *song = "D_RUNNIN";
	EXPECT_EQ(ComputeMusicLumpName(song), song);
	const char *file = "file://music/big.ogg";
	EXPECT_EQ(ComputeMusicLumpName(file), file + 7);
	EXPECT_STREQ(ComputeMusicLumpName(",C"), ",C");
}

// [Doruk-Aksoy] No name, the level's own song and a CD track in any case never name a lump.
TEST(ComputeMusicLumpName, NeverALump)
{
	EXPECT_EQ(ComputeMusicLumpName(nullptr), nullptr);
	EXPECT_EQ(ComputeMusicLumpName(""), nullptr);
	EXPECT_EQ(ComputeMusicLumpName("*"), nullptr);
	EXPECT_EQ(ComputeMusicLumpName(",CD,2"), nullptr);
	EXPECT_EQ(ComputeMusicLumpName(",cd,2,1f"), nullptr);
}

// [Doruk-Aksoy] Off holds nothing; on, the slider's value, held to its ends.
TEST(ComputeMusicCacheBudget, OffOrClampedMegabytes)
{
	EXPECT_EQ(ComputeMusicCacheBudget(false, 128), 0);
	EXPECT_EQ(ComputeMusicCacheBudget(true, 128), 128 * kMB);
	EXPECT_EQ(ComputeMusicCacheBudget(true, 0), ZX_MUSICCACHE_MIN_MB * kMB);
	EXPECT_EQ(ComputeMusicCacheBudget(true, 4096), ZX_MUSICCACHE_MAX_MB * kMB);
}

// [Doruk-Aksoy] The level's songs are kept in their order, MAPINFO's first as the caller puts it.
TEST(ComputeMusicCachePlan, KeepsInPriorityOrder)
{
	const std::vector<ZxMusicCandidate> songs = { { 7, 6 * kMB, true }, { 3, 3 * kMB, true }, { 9, 1 * kMB, true } };
	EXPECT_EQ(ComputeMusicCachePlan(songs, 128 * kMB), (std::vector<int>{ 7, 3, 9 }));
}

// [Doruk-Aksoy] A missing, empty or stored song is left out, since only an inflated one costs a frame.
TEST(ComputeMusicCachePlan, SkipsWhatNeedsNoCache)
{
	const std::vector<ZxMusicCandidate> songs = { { -1, 6 * kMB, true }, { 3, 0, true }, { 4, 5 * kMB, false }, { 5, 2 * kMB, true } };
	EXPECT_EQ(ComputeMusicCachePlan(songs, 128 * kMB), (std::vector<int>{ 5 }));
}

// [Doruk-Aksoy] A song named twice, by MAPINFO and a script or by two scripts, is kept once and counted once.
TEST(ComputeMusicCachePlan, KeepsEachSongOnce)
{
	const std::vector<ZxMusicCandidate> songs = { { 7, 10 * kMB, true }, { 7, 10 * kMB, true }, { 3, 10 * kMB, true } };
	EXPECT_EQ(ComputeMusicCachePlan(songs, 20 * kMB), (std::vector<int>{ 7, 3 }));
}

// [Doruk-Aksoy] A song too big for what is left is passed over for smaller ones after it.
TEST(ComputeMusicCachePlan, SkipsWhatDoesNotFit)
{
	const std::vector<ZxMusicCandidate> songs = { { 1, 10 * kMB, true }, { 2, 20 * kMB, true }, { 3, 5 * kMB, true } };
	EXPECT_EQ(ComputeMusicCachePlan(songs, 16 * kMB), (std::vector<int>{ 1, 3 }));
	EXPECT_TRUE(ComputeMusicCachePlan(songs, 0).empty());
}
}
