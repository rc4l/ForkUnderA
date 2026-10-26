// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-lineattack/computation/lineattack_compute.h"

#include <gtest/gtest.h>

using namespace zx::acs;

// [Doruk-Aksoy] Tid 0 fires from the activator, any other tid from the actors carrying it, and tid 0 with no activator fires nothing rather than dereferencing it.
TEST(LineAttack, FiresFromTheActivatorTheTidOrNothing)
{
	EXPECT_EQ(kLineAttackFromActivator, ComputeLineAttackShooter(0, true));
	EXPECT_EQ(kLineAttackNothing, ComputeLineAttackShooter(0, false));
	EXPECT_EQ(kLineAttackFromTid, ComputeLineAttackShooter(5, true));
	EXPECT_EQ(kLineAttackFromTid, ComputeLineAttackShooter(5, false));
}

// [Doruk-Aksoy] A script is warned about once, however often it calls; another script gets its own warning.
TEST(LineAttack, EachScriptIsWarnedAboutOnce)
{
	std::vector<int> warned;
	EXPECT_TRUE(ComputeFirstWarning(warned, -18092));
	EXPECT_FALSE(ComputeFirstWarning(warned, -18092));
	EXPECT_TRUE(ComputeFirstWarning(warned, 3));
	EXPECT_FALSE(ComputeFirstWarning(warned, 3));
	EXPECT_EQ(2u, warned.size());
}
