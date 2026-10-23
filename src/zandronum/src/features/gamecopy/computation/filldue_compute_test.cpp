// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/filldue_compute.h"

#include <gtest/gtest.h>

using zx::gamecopy::ComputeFillDue;
using zx::gamecopy::FillStamp;

namespace {
FillStamp At(int tic, unsigned packets = 0)
{
	FillStamp s;
	s.tic = tic;
	s.packets = packets;
	return s;
}
}

TEST(FillDue, OnceATic)
{
	EXPECT_TRUE(ComputeFillDue(At(10), At(9)));
	EXPECT_FALSE(ComputeFillDue(At(10), At(10)));
}

TEST(FillDue, TheFirstFrameOfALevelFills)
{
	EXPECT_TRUE(ComputeFillDue(At(0), FillStamp()));
	EXPECT_TRUE(ComputeFillDue(At(57), FillStamp()));
}

TEST(FillDue, AClientFillsForEachPacketAppliedBetweenItsTics)
{
	EXPECT_TRUE(ComputeFillDue(At(10, 41), At(10, 40)));
	// [rc4l] A frame with no tic and no packet since the last fill has nothing new to copy.
	EXPECT_FALSE(ComputeFillDue(At(10, 41), At(10, 41)));
}
