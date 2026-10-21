// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <utility>
#include <vector>
#include "features/hwrender/computation/growbuffer_compute.h"

using namespace zx::hwrender;

// [rc4l] Sunder's 14 MB of vertices and 20 MB of piece records took a 16 and a 32 MB upload page, which a browser's device never frees.
TEST(GrowBufferCompute, AWriteIsSentInPiecesNoBiggerThanAPage)
{
	std::vector<std::pair<size_t, size_t>> pieces;
	const auto keep = [&](size_t at, size_t n) { pieces.push_back({ at, n }); };
	zx::hwrender::ComputeUploadChunks(10, 4, keep);
	EXPECT_EQ(pieces, (std::vector<std::pair<size_t, size_t>>{ { 0, 4 }, { 4, 4 }, { 8, 2 } }));
	pieces.clear();
	zx::hwrender::ComputeUploadChunks(8, 4, keep);
	EXPECT_EQ(pieces, (std::vector<std::pair<size_t, size_t>>{ { 0, 4 }, { 4, 4 } }));
	pieces.clear();
	zx::hwrender::ComputeUploadChunks(0, 4, keep);
	EXPECT_TRUE(pieces.empty());
	zx::hwrender::ComputeUploadChunks(5, 0, keep);
	EXPECT_EQ(pieces, (std::vector<std::pair<size_t, size_t>>{ { 0, 5 } }));
}

TEST(GrowBufferCompute, ABufferFitsOnceMadeWithRoomForTheCount)
{
	EXPECT_TRUE(ComputeBufferFits(true, 100, 100));
	EXPECT_FALSE(ComputeBufferFits(true, 100, 101));
	EXPECT_FALSE(ComputeBufferFits(false, 100, 0));
}

TEST(GrowBufferCompute, AShortBufferGrowsByHalfAgainPlusItsPad)
{
	EXPECT_EQ(1024u, ComputeGrownCapacity(0, 1024));
	EXPECT_EQ(150u + 256u, ComputeGrownCapacity(100, 256));
	EXPECT_EQ(4u, ComputeGrownCapacity(3, 0));   // [rc4l] the half rounds down
}

// [rc4l] nuts.wad's 10,600 sprites on a 192-piece map overran the records behind the level's, and every sprite past them drew with the last record's material.
TEST(GrowBufferCompute, APieceBufferKeepsRoomForTheBusiestFramesSprites)
{
	EXPECT_EQ(192u + 96u + 4096u, ComputePieceCapacity(192, 0));
	EXPECT_EQ(192u + 96u + 4096u, ComputePieceCapacity(192, 2730));   // [rc4l] 4095 with half again, under the floor
	EXPECT_EQ(192u + 96u + 15900u, ComputePieceCapacity(192, 10600));
	EXPECT_EQ(60000u + 30000u + 4096u, ComputePieceCapacity(60000, 100));
}

// [rc4l] A piece buffer holds the level's records and then one for each of a frame's dynamic pieces.
TEST(GrowBufferCompute, APieceBufferFitsWhileTheLevelsAndTheFramesPiecesDo)
{
	EXPECT_TRUE(ComputeBufferFits(true, 4384, 192 + 4192));
	EXPECT_FALSE(ComputeBufferFits(true, 4384, 192 + 4193));
	EXPECT_FALSE(ComputeBufferFits(true, 4384, 192 + 10600));
}

// [rc4l] A level's upload kept a page in a browser's pool for every 8 MB it sent in one frame, 96 MB on Sunder MAP11, and Firefox ran out of GPU memory.
TEST(GrowBufferCompute, AFlushFallsDueEachTimeTheBudgetFills)
{
	size_t since = 0;
	EXPECT_FALSE(zx::hwrender::ComputeUploadFlushDue(since, 3, 4));
	EXPECT_EQ(3u, since);
	EXPECT_TRUE(zx::hwrender::ComputeUploadFlushDue(since, 1, 4));
	EXPECT_EQ(0u, since);
	EXPECT_TRUE(zx::hwrender::ComputeUploadFlushDue(since, 9, 4));
	EXPECT_EQ(0u, since);
	// [rc4l] No budget, as on Vulkan, whose upload heap is a ring: never due.
	EXPECT_FALSE(zx::hwrender::ComputeUploadFlushDue(since, 1u << 30, 0));
	EXPECT_EQ(1u << 30, since);
	int flushes = 0;
	size_t pending = 0;
	zx::hwrender::ComputeUploadChunks(48u << 20, 1u << 20, [&](size_t, size_t n) { if (zx::hwrender::ComputeUploadFlushDue(pending, n, zx::hwrender::kUploadFlushBytes)) flushes++; });
	EXPECT_EQ(12, flushes);
}

// [rc4l] A level's first upload asked the device it had not yet made for its type, and every scenario's engine crashed.
TEST(GrowBufferCompute, OnlyABrowsersMadeDeviceFlushes)
{
	EXPECT_EQ(0u, zx::hwrender::ComputeUploadFlushBudget(false, true));
	EXPECT_EQ(0u, zx::hwrender::ComputeUploadFlushBudget(false, false));
	EXPECT_EQ(0u, zx::hwrender::ComputeUploadFlushBudget(true, false));
	EXPECT_EQ(zx::hwrender::kUploadFlushBytes, zx::hwrender::ComputeUploadFlushBudget(true, true));
}
