// [MobiusU9B] Tests for what a save does with each binding, covering every branch of bindarchive_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/save-settings/computation/bindarchive_compute.h"

#include <gtest/gtest.h>

using zx::ComputeBindArchive;
using zx::EBindArchive;

TEST(ComputeBindArchive, ACustomSectionWritesAKeyBoundToItsAction)
{
	EXPECT_EQ(EBindArchive::Write, ComputeBindArchive("upgs", "upgs", false));
}

TEST(ComputeBindArchive, ACustomSectionMatchesTheActionWithoutCase)
{
	EXPECT_EQ(EBindArchive::Write, ComputeBindArchive("+WepWheel", "+wepwheel", false));
	EXPECT_EQ(EBindArchive::Write, ComputeBindArchive("+wepwheel", "+WEPWHEEL", false));
}

TEST(ComputeBindArchive, ACustomSectionSkipsAKeyBoundToAnotherAction)
{
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("+attack", "upgs", false));
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("jump", "upgs", false));
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("upgs", "upgs2", false));
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("upgs2", "upgs", false));
}

TEST(ComputeBindArchive, ACustomSectionSkipsAnUnboundKey)
{
	// [MobiusU9B] A custom section never clears a key, since the general pass owns the general section's entries.
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("", "upgs", false));
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive(nullptr, "upgs", false));
}

TEST(ComputeBindArchive, ACustomSectionSkipsAKeyAnEarlierSectionClaimed)
{
	// [MobiusU9B] Two sections listing one action write the key only to the first, as the old "\1" marker did.
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("upgs", "upgs", true));
}

TEST(ComputeBindArchive, TheGeneralPassWritesAnUnclaimedKey)
{
	EXPECT_EQ(EBindArchive::Write, ComputeBindArchive("+attack", nullptr, false));
}

TEST(ComputeBindArchive, TheGeneralPassSkipsAKeyACustomSectionClaimed)
{
	// [MobiusU9B] The claimed key keeps its binding in the game; only its general entry is left alone.
	EXPECT_EQ(EBindArchive::Skip, ComputeBindArchive("upgs", nullptr, true));
}

TEST(ComputeBindArchive, TheGeneralPassClearsAnUnboundKey)
{
	EXPECT_EQ(EBindArchive::Clear, ComputeBindArchive("", nullptr, false));
	EXPECT_EQ(EBindArchive::Clear, ComputeBindArchive(nullptr, nullptr, false));
}
