// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/browserhit_compute.h"

using zx::ComputeServerAtSlot;

namespace
{
// The browser's own geometry: 12 drawn row slots.
const int kVisible = 12;
} // namespace

TEST(ServerAtSlot, MapsSlotsStraightThroughOnTheFirstPage)
{
	EXPECT_EQ(0, ComputeServerAtSlot(0, kVisible, 0, 30));
	EXPECT_EQ(5, ComputeServerAtSlot(5, kVisible, 0, 30));
	EXPECT_EQ(11, ComputeServerAtSlot(11, kVisible, 0, 30));
}

TEST(ServerAtSlot, AddsTheScrollOffset)
{
	// The whole reason this is not just the slot number: the list scrolls underneath fixed slots, so
	// clicking the top row after scrolling must join the server drawn there, not server 0.
	EXPECT_EQ(7, ComputeServerAtSlot(0, kVisible, 7, 30));
	EXPECT_EQ(18, ComputeServerAtSlot(11, kVisible, 7, 30));
}

TEST(ServerAtSlot, RejectsTheBlankTailOfAPartlyFilledLastPage)
{
	// 30 servers, scrolled so slots 0-5 hold servers 24-29 and slots 6-11 are empty. Clicking the
	// empty space must not wrap round to a server or land on the last one.
	EXPECT_EQ(29, ComputeServerAtSlot(5, kVisible, 24, 30));
	EXPECT_EQ(-1, ComputeServerAtSlot(6, kVisible, 24, 30));
	EXPECT_EQ(-1, ComputeServerAtSlot(11, kVisible, 24, 30));
}

TEST(ServerAtSlot, RejectsSlotsOutsideTheDrawnRows)
{
	EXPECT_EQ(-1, ComputeServerAtSlot(-1, kVisible, 0, 30));
	EXPECT_EQ(-1, ComputeServerAtSlot(kVisible, kVisible, 0, 30));
	EXPECT_EQ(-1, ComputeServerAtSlot(999, kVisible, 0, 30));
}

TEST(ServerAtSlot, AnEmptyListHasNothingToClick)
{
	// The browser draws its "nothing is being hosted" message over the row area, so clicks still
	// arrive -- they just must not select anything.
	EXPECT_EQ(-1, ComputeServerAtSlot(0, kVisible, 0, 0));
	EXPECT_EQ(-1, ComputeServerAtSlot(3, kVisible, 0, -1));
}

TEST(ServerAtSlot, ANegativeScrollOffsetSelectsNothingRatherThanClampingToZero)
{
	// Clamping would map a corrupt offset onto real servers and join one of them. Refusing is the
	// only answer that cannot join something the player did not click.
	EXPECT_EQ(-1, ComputeServerAtSlot(0, kVisible, -1, 30));
	EXPECT_EQ(-1, ComputeServerAtSlot(4, kVisible, -10, 30));
}

TEST(ServerAtSlot, AListShorterThanOnePageOnlyAnswersForRowsThatExist)
{
	EXPECT_EQ(0, ComputeServerAtSlot(0, kVisible, 0, 2));
	EXPECT_EQ(1, ComputeServerAtSlot(1, kVisible, 0, 2));
	EXPECT_EQ(-1, ComputeServerAtSlot(2, kVisible, 0, 2));
}
