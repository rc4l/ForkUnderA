// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/hwrender/computation/layoutvars_compute.h"

using namespace zx::hwrender;

namespace {
const unsigned kVS = 1, kPS = 2, kFlag = 1;
const int kStatic = 0, kMutable = 1;
}

// [rc4l] A buffer the list already names gets the flag and keeps its type; one it leaves out is added at the default type.
TEST(LayoutVars, ListedBuffersAreFlaggedAndTheRestAdded)
{
	const std::vector<LayoutVar> vars = { { kPS, "uTex", kMutable, 0 }, { kPS, "Decals", kMutable, 0 } };
	const std::vector<LayoutVar> out = ComputeFlagStorageBuffers(vars, { { kPS, "Decals" }, { kVS, "Pieces" } }, kStatic, kFlag);
	ASSERT_EQ(3u, out.size());
	EXPECT_EQ(0u, out[0].flags);
	EXPECT_EQ(kFlag, out[1].flags);
	EXPECT_EQ(kMutable, out[1].type);
	EXPECT_EQ(kVS, out[2].stages);
	EXPECT_EQ("Pieces", out[2].name);
	EXPECT_EQ(kStatic, out[2].type);
	EXPECT_EQ(kFlag, out[2].flags);
}

// [rc4l] A name listed for another stage only is a different variable, so the buffer's stage gets an entry of its own.
TEST(LayoutVars, TheStageMustMatch)
{
	const std::vector<LayoutVar> out = ComputeFlagStorageBuffers({ { kVS, "Pieces", kMutable, 0 } }, { { kPS, "Pieces" } }, kStatic, kFlag);
	ASSERT_EQ(2u, out.size());
	EXPECT_EQ(0u, out[0].flags);
	EXPECT_EQ(kPS, out[1].stages);
}

// [rc4l] One entry covering both stages is flagged once for each, and nothing is added.
TEST(LayoutVars, AnEntryForBothStagesCoversBoth)
{
	const std::vector<LayoutVar> out = ComputeFlagStorageBuffers({ { kVS | kPS, "Constants", kStatic, 4 } },
		{ { kVS, "Constants" }, { kPS, "Constants" } }, kStatic, kFlag);
	ASSERT_EQ(1u, out.size());
	EXPECT_EQ(4u | kFlag, out[0].flags);
}
