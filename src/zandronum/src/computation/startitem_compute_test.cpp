// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/startitem_compute.h"

#include <gtest/gtest.h>

using zx::ComputeStartItemMorphedPlayer;

TEST(StartItemMorph, AnItemPickedUpByAnotherBodyMorphedThePlayer)
{
	EXPECT_TRUE(ComputeStartItemMorphedPlayer(true, false));
	EXPECT_FALSE(ComputeStartItemMorphedPlayer(true, true));
}

TEST(StartItemMorph, ARefusedItemMorphedNobody)
{
	// [rc4l] A refused item may never have said who took it, which stopped the game with a morph error.
	EXPECT_FALSE(ComputeStartItemMorphedPlayer(false, false));
	EXPECT_FALSE(ComputeStartItemMorphedPlayer(false, true));
}
