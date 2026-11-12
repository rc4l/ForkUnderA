// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/inventoryselect_compute.h"

using zx::ComputeSelectsInventory;
using zx::ComputeInventoryBarTics;

TEST(InventorySelect, OnlyAHeldInventoryBarItemIsSelected)
{
	EXPECT_TRUE(ComputeSelectsInventory(true, true));
	EXPECT_FALSE(ComputeSelectsInventory(true, false));
	EXPECT_FALSE(ComputeSelectsInventory(false, true));
	EXPECT_FALSE(ComputeSelectsInventory(false, false));
}

TEST(InventorySelect, AskingRaisesTheBarForFiveSecondsElseItKeepsItsTics)
{
	EXPECT_EQ(ComputeInventoryBarTics(true, 0, 35), 175);
	EXPECT_EQ(ComputeInventoryBarTics(true, 40, 35), 175);
	EXPECT_EQ(ComputeInventoryBarTics(false, 40, 35), 40);
}
