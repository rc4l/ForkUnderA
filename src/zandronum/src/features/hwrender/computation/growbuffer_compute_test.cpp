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

TEST(GrowBufferCompute, AShortBufferGrowsByHalfAgainPlusItsPad)
{
	EXPECT_EQ(1024u, ComputeGrownCapacity(0, 1024));
	EXPECT_EQ(150u + 256u, ComputeGrownCapacity(100, 256));
	EXPECT_EQ(4u, ComputeGrownCapacity(3, 0));   // [rc4l] the half rounds down
}
