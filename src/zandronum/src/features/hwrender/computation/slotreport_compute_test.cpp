// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/slotreport_compute.h"

using namespace zx::hwrender;

// [Doruk-Aksoy] The working set counts the last frame's stamps and each window's, adding to what an earlier set counted.
TEST(SlotReport, TheWorkingSetCountsFramesAndWindows)
{
	std::vector<SlotStamp> stamps(5);
	stamps[1] = { 5, 1000 };
	stamps[2] = { 5, 1000 };
	stamps[3] = { 4, 500 };
	stamps[4] = { 2, -3000 };
	const std::vector<double> windows = { 1000, 2000, 5000 };
	std::vector<int> counts;
	ComputeWorkingSet(stamps, 5, 1000, windows, counts);
	EXPECT_EQ((std::vector<int>{ 2, 3, 3, 4 }), counts);
	ComputeWorkingSet({ { 5, 900 } }, 5, 1000, windows, counts);
	EXPECT_EQ((std::vector<int>{ 3, 4, 4, 5 }), counts) << "a second set adds to the first";
}

// [Doruk-Aksoy] Peaks only rise, and grow to as many counts as they are given.
TEST(SlotReport, PeaksOnlyRise)
{
	std::vector<int> peaks;
	ComputeKeepPeaks(peaks, { 3, 4 });
	EXPECT_EQ((std::vector<int>{ 3, 4 }), peaks);
	ComputeKeepPeaks(peaks, { 1, 9, 2 });
	EXPECT_EQ((std::vector<int>{ 3, 9, 2 }), peaks);
}
