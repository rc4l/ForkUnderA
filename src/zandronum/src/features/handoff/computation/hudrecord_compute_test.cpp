// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/hudrecord_compute.h"

#include <gtest/gtest.h>

using zx::handoff::ComputeHUDRecordDue;
using zx::handoff::HudKey;

namespace {
const HudKey kMade = { 100, 1920, 1080, 1080, 11, false, true, false, true };
}

TEST(HudRecord, NoneYetIsDue)
{
	EXPECT_TRUE(ComputeHUDRecordDue(false, kMade, kMade));
}

TEST(HudRecord, TheSameTicAndLayoutKeepsIt)
{
	EXPECT_FALSE(ComputeHUDRecordDue(true, kMade, kMade));
}

TEST(HudRecord, ANewTicOrAnyLayoutChangeRecordsAgain)
{
	HudKey now = kMade; now.tic = 101; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.screenWidth = 1280; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.screenHeight = 720; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.viewHeight = 900; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.screenBlocks = 10; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.automap = true; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.viewActive = false; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.altHud = true; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
	now = kMade; now.fullscreenHud = false; EXPECT_TRUE(ComputeHUDRecordDue(true, kMade, now));
}
