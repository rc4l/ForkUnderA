// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/mcp-bridge/computation/streammoves_compute.h"

#include <gtest/gtest.h>

using namespace zx::mcp;

TEST(StreamMoves, OnlyNamedStreamsInTheListAreFrameStreams)
{
	const std::vector<uint32_t> frame = { 5, 9 };
	EXPECT_TRUE(ComputeFrameStream(9, frame));
	EXPECT_FALSE(ComputeFrameStream(7, frame));
	EXPECT_FALSE(ComputeFrameStream(0, { 0 }));
}

TEST(StreamMoves, TheFirstTicListsEveryStreamAndLaterTicsOnlyThoseThatMoved)
{
	StreamPlaces last;
	const std::vector<uint32_t> frame;
	EXPECT_EQ(ComputeStreamMoves({ { 7, 3, 11 }, { 8, 1, 4 } }, frame, last), " 7:3 8:1");
	EXPECT_EQ(ComputeStreamMoves({ { 7, 3, 11 }, { 8, 2, 4 } }, frame, last), " 8:2");
	EXPECT_EQ(ComputeStreamMoves({ { 7, 3, 12 }, { 8, 2, 4 } }, frame, last), " 7:3");
	EXPECT_EQ(ComputeStreamMoves({ { 7, 3, 12 }, { 8, 2, 4 } }, frame, last), "");
}

TEST(StreamMoves, FrameStreamsAreLeftOutAndNamelessStreamsAreTrackedByTheirOrder)
{
	StreamPlaces last;
	const std::vector<uint32_t> frame = { 9 };
	EXPECT_EQ(ComputeStreamMoves({ { 0, 1, 1 }, { 0, 2, 2 }, { 9, 5, 5 } }, frame, last), " 0:1 0:2");
	EXPECT_EQ(ComputeStreamMoves({ { 0, 1, 1 }, { 0, 3, 2 }, { 9, 6, 5 } }, frame, last), " 0:3");
	EXPECT_EQ(last.size(), 2u);
}
