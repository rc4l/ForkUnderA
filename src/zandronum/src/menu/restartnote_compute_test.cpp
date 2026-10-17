// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "menu/restartnote_compute.h"

using zx::ComputeRestartNote;
using zx::RestartNoteFit;

namespace
{
	// [rc4l] Three wordings 200, 133 and 60 font pixels wide after a 4-pixel space, as SmallFont measures the renderer's notes.
	const int kWidths[] = { 200, 133, 60 };

	RestartNoteFit Note(const char *value, const char *launched, int valueRight, int right, int scale, bool recorded = true, bool grayed = false)
	{
		return ComputeRestartNote(value, launched, recorded, grayed, valueRight, 4, right, scale, kWidths, 3);
	}

	void ExpectFit(RestartNoteFit got, int wording, int scale)
	{
		EXPECT_EQ(got.wording, wording);
		EXPECT_EQ(got.scale, scale);
	}
}

TEST(RestartNote, HiddenUntilTheValueLeavesTheOneTheGameStartedWith)
{
	ExpectFit(Note("1", "1", 0, 10000, 2), -1, 0);
	ExpectFit(Note("0", "1", 0, 10000, 2), 0, 2);
	ExpectFit(Note("2", "1", 0, 10000, 2), 0, 2);
}

// [rc4l] A build that never chose a renderer, or a row the player cannot change, has nothing to restart for.
TEST(RestartNote, HiddenWithoutALaunchValueOrOnAGreyedRow)
{
	ExpectFit(Note("0", "1", 0, 10000, 2, false), -1, 0);
	ExpectFit(Note("0", "1", 0, 10000, 2, true, true), -1, 0);
	ExpectFit(Note(nullptr, "1", 0, 10000, 2), -1, 0);
	ExpectFit(Note("0", nullptr, 0, 10000, 2), -1, 0);
}

// [rc4l] The menu's own size with a shorter wording comes before smaller text.
TEST(RestartNote, ShortensBeforeItShrinks)
{
	ExpectFit(Note("0", "1", 1896, 2560, 7), 2, 7);
	ExpectFit(Note("0", "1", 0, 959, 7), 1, 7);
	ExpectFit(Note("0", "1", 0, 958, 7), 2, 7);
}

// [rc4l] At 1440p the menu draws at 6x and "Vulkan (Recommended)" ends at 2324 of 2554 usable pixels, which "(restart)" fits only at 3x; tighter rows shrink it further, down to 1x.
TEST(RestartNote, ShrinksAsFarAsItMustBeforeGivingUp)
{
	ExpectFit(Note("0", "1", 2324, 2554, 6), 2, 3);
	ExpectFit(Note("0", "1", 2324, 2452, 6), 2, 2);
	ExpectFit(Note("0", "1", 2324, 2451, 6), 2, 1);
	ExpectFit(Note("0", "1", 2324, 2387, 6), -1, 0);
	ExpectFit(Note("0", "1", 0, 191, 9), 2, 2);
}

// [rc4l] At 1280x720 the menu draws at 2x and the note after "Vulkan (Recommended)" fits only at 1x, which beats saying nothing.
TEST(RestartNote, ASmallMenuStillShrinksToOne)
{
	ExpectFit(Note("0", "1", 0, 128, 2), 2, 2);
	ExpectFit(Note("0", "1", 0, 127, 2), 2, 1);
	ExpectFit(Note("0", "1", 0, 64, 1), 2, 1);
	ExpectFit(Note("0", "1", 0, 63, 1), -1, 0);
}
