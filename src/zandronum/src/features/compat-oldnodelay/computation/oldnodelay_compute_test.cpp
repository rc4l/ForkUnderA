// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/compat-oldnodelay/computation/oldnodelay_compute.h"

#include <gtest/gtest.h>

using namespace zx::compat;

// [Doruk-Aksoy] A NoDelay state that lasts keeps its whole duration past its action, as Zandronum 3.3 left it, so `TNT1 A 1 NoDelay` is still there after this tic's countdown.
TEST(OldNoDelay, ALastingStateKeepsItsWholeDuration)
{
	EXPECT_EQ(2, ComputeOldNoDelayTics(1, 1));
	EXPECT_EQ(35, ComputeOldNoDelayTics(34, 34));
	// [Doruk-Aksoy] An action that jumped to another lasting state gets the same extra tic on that state.
	EXPECT_EQ(4, ComputeOldNoDelayTics(5, 3));
}

// [Doruk-Aksoy] A 0-tic spawn state lets the next state run as it would, and one the action left infinite stays infinite.
TEST(OldNoDelay, AZeroTicOrInfiniteStateIsLeftAlone)
{
	EXPECT_EQ(0, ComputeOldNoDelayTics(0, 0));
	EXPECT_EQ(3, ComputeOldNoDelayTics(0, 3));
	EXPECT_EQ(-1, ComputeOldNoDelayTics(5, -1));
	EXPECT_EQ(-1, ComputeOldNoDelayTics(-1, -1));
}
