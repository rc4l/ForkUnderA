// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <limits>

#include "computation/settingtext_compute.h"

using zx::ComputeFloatSettingText;

TEST(SettingText, InfinityAndNaNAreNamed)
{
	EXPECT_EQ(ComputeFloatSettingText(std::numeric_limits<float>::infinity(), 0), "infinity");
	EXPECT_EQ(ComputeFloatSettingText(-std::numeric_limits<float>::infinity(), 2), "-infinity");
	EXPECT_EQ(ComputeFloatSettingText(std::numeric_limits<float>::quiet_NaN(), 2), "NaN");
}

TEST(SettingText, AFiniteValueTakesItsDecimalsOrPrintfsSix)
{
	EXPECT_EQ(ComputeFloatSettingText(2.5f, 2), "2.50");
	EXPECT_EQ(ComputeFloatSettingText(800.f, 0), "800.000000");
	EXPECT_EQ(ComputeFloatSettingText(-1.25f, -1), "-1.250000");
}

// [rc4l] The largest float's 39 digits and six decimals still fit the text.
TEST(SettingText, TheLargestFloatFits)
{
	EXPECT_EQ(ComputeFloatSettingText(std::numeric_limits<float>::max(), 0).size(), 46u);
}
