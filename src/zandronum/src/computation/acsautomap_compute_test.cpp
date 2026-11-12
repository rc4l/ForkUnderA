// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/acsautomap_compute.h"

using zx::ComputeAutomapStatus;

TEST(AcsAutomap, AnInactiveAutomapIsOffWhateverTheView)
{
	EXPECT_EQ(ComputeAutomapStatus(false, true), zx::AUTOMAP_OFF);
	EXPECT_EQ(ComputeAutomapStatus(false, false), zx::AUTOMAP_OFF);
}

TEST(AcsAutomap, AnActiveAutomapOverlaysADrawnViewElseFillsTheScreen)
{
	EXPECT_EQ(ComputeAutomapStatus(true, true), zx::AUTOMAP_OVERLAYMAP);
	EXPECT_EQ(ComputeAutomapStatus(true, false), zx::AUTOMAP_FULLMAP);
}

TEST(AcsAutomap, TheAnswersAreUpstreamsNumbers)
{
	EXPECT_EQ(zx::AUTOMAP_OFF, 0);
	EXPECT_EQ(zx::AUTOMAP_FULLMAP, 1);
	EXPECT_EQ(zx::AUTOMAP_OVERLAYMAP, 2);
}
