// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/slot_compute.h"

using namespace zx::hwrender;

// [rc4l] WDI01's ceiling stack is one portal over sixty sectors, so every sector of it must land in the one slot.
TEST(SlotCompute, AnEqualKeyReusesItsSlotAndANewOneTakesTheNext)
{
	const void *slots[2];
	int used = 0;
	int a = 0, b = 0;
	EXPECT_EQ(0, ComputeSlotFor<const void *>(slots, used, 2, &a));
	EXPECT_EQ(0, ComputeSlotFor<const void *>(slots, used, 2, &a));
	EXPECT_EQ(1, ComputeSlotFor<const void *>(slots, used, 2, &b));
	EXPECT_EQ(2, used);
}

TEST(SlotCompute, AKeyBeyondTheLastSlotGetsNoneAndTakesNothing)
{
	int slots[1];
	int used = 0;
	EXPECT_EQ(0, ComputeSlotFor(slots, used, 1, 7));
	EXPECT_EQ(-1, ComputeSlotFor(slots, used, 1, 8));
	EXPECT_EQ(0, ComputeSlotFor(slots, used, 1, 7));
	EXPECT_EQ(1, used);
}

TEST(SlotCompute, NoSlotsAtAllGivesNone)
{
	int slots[1];
	int used = 0;
	EXPECT_EQ(-1, ComputeSlotFor(slots, used, 0, 7));
	EXPECT_EQ(0, used);
}
