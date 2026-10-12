// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/closedline_compute.h"

#include <gtest/gtest.h>

using namespace zx::visibility;

namespace {

// [rc4l] A front sector from 0 to 128 and a back sector given by its floor and ceiling, flat, both textures present.
ClosedLineIn Line(long long bsFloor, long long bsCeiling)
{
	ClosedLineIn in = {};
	in.fsFloor[0] = in.fsFloor[1] = 0;
	in.fsCeiling[0] = in.fsCeiling[1] = 128;
	in.bsFloor[0] = in.bsFloor[1] = bsFloor;
	in.bsCeiling[0] = in.bsCeiling[1] = bsCeiling;
	in.topTexture = in.bottomTexture = true;
	return in;
}

} // namespace

TEST(ClosedLine, AnOpenLineDoesNotBlock)
{
	EXPECT_FALSE(ComputeLineCloses(Line(0, 128)));
	EXPECT_FALSE(ComputeLineCloses(Line(16, 96)));
}

TEST(ClosedLine, AShutDoorBlocksUnlessItsEdgeIsUntextured)
{
	ClosedLineIn in = Line(0, 0);
	EXPECT_TRUE(ComputeLineCloses(in));
	// [rc4l] Back ceiling at the front floor is the lowered-ceiling case first, which wants a top texture.
	in.topTexture = false;
	EXPECT_FALSE(ComputeLineCloses(in));
	in = Line(64, 64);   // [rc4l] shut halfway up: its own ceiling below the front's, its floor above
	EXPECT_TRUE(ComputeLineCloses(in));
	in.topTexture = false;
	EXPECT_FALSE(ComputeLineCloses(in));   // [rc4l] the see-through door effect
	in = Line(64, 64);
	in.bottomTexture = false;
	EXPECT_FALSE(ComputeLineCloses(in));
	in = Line(64, 64);
	in.bothCeilingsSky = true;
	EXPECT_FALSE(ComputeLineCloses(in));
	in = Line(64, 64);
	in.bothFloorsSky = true;
	EXPECT_FALSE(ComputeLineCloses(in));
}

TEST(ClosedLine, ALiftAtTheTopBlocksWithABottomTexture)
{
	ClosedLineIn in = Line(128, 200);   // [rc4l] the back floor at the front ceiling
	EXPECT_TRUE(ComputeLineCloses(in));
	in.bottomTexture = false;
	EXPECT_FALSE(ComputeLineCloses(in));
	in = Line(128, 200);
	in.bothCeilingsSky = true;   // [rc4l] an open-sky courtyard is never shut
	EXPECT_FALSE(ComputeLineCloses(in));
}

TEST(ClosedLine, ALoweredCeilingBlocksUnlessBothSkiesShow)
{
	ClosedLineIn in = Line(-64, -8);
	EXPECT_TRUE(ComputeLineCloses(in));
	in.bothCeilingsSky = true;
	EXPECT_FALSE(ComputeLineCloses(in));
}

TEST(ClosedLine, StackedSectorsNeverBlock)
{
	ClosedLineIn in = Line(0, 0);
	in.stacked = true;
	EXPECT_FALSE(ComputeLineCloses(in));
}

TEST(ClosedLine, OnlyHeightsThatShutTheLineAskAboutTextures)
{
	EXPECT_FALSE(ComputeLineMayClose(Line(0, 128)));
	EXPECT_TRUE(ComputeLineMayClose(Line(0, 0)));
	EXPECT_TRUE(ComputeLineMayClose(Line(128, 200)));
	EXPECT_TRUE(ComputeLineMayClose(Line(64, 64)));
}

TEST(ClosedLine, SlopedEndsMustBothBeShut)
{
	ClosedLineIn in = Line(0, 0);
	in.bsCeiling[1] = 40;   // [rc4l] one end open
	EXPECT_FALSE(ComputeLineCloses(in));
}
