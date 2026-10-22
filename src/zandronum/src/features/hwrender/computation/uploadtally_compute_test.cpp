// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/uploadtally_compute.h"

using namespace zx::hwrender;

TEST(ComputeUploadTally, EachBufferPerFrameMostFirst)
{
	UploadTally t;
	ComputeUploadTallyAdd(t, "fua dynamic VB", 400);
	ComputeUploadTallyAdd(t, "fua scene piece data", 1000);
	ComputeUploadTallyAdd(t, "fua scene piece data", 1000);
	ComputeUploadTallyAdd(t, nullptr, 10);
	ComputeUploadTallyAdd(t, "", 10);
	ComputeUploadTallyFrame(t);
	ComputeUploadTallyFrame(t);
	const auto r = ComputeUploadTallyTake(t);
	ASSERT_EQ(3u, r.size());
	EXPECT_EQ("fua scene piece data", r[0].first);
	EXPECT_DOUBLE_EQ(1000.0, r[0].second);
	EXPECT_EQ("fua dynamic VB", r[1].first);
	EXPECT_DOUBLE_EQ(200.0, r[1].second);
	EXPECT_EQ("unnamed", r[2].first);
	EXPECT_DOUBLE_EQ(10.0, r[2].second);
	// [rc4l] A take starts the tally over.
	EXPECT_TRUE(ComputeUploadTallyTake(t).empty());
}

TEST(ComputeUploadTally, NoFrameCountedReadsAsOne)
{
	UploadTally t;
	ComputeUploadTallyAdd(t, "fua 2D VB", 64);
	const auto r = ComputeUploadTallyTake(t);
	ASSERT_EQ(1u, r.size());
	EXPECT_DOUBLE_EQ(64.0, r[0].second);
}
