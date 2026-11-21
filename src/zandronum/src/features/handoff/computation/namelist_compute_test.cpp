// SPDX-License-Identifier: GPL-3.0-or-later

#include "features/handoff/computation/namelist_compute.h"

#include <gtest/gtest.h>

using zx::handoff::ComputeNameListAdd;
using zx::handoff::ComputeNameListHas;

TEST(NameListCompute, AnEmptyListHoldsNothing)
{
	std::vector<std::string> names;
	EXPECT_FALSE(ComputeNameListHas(names, "SMALLFONT"));
	EXPECT_FALSE(ComputeNameListHas(names, ""));
}

TEST(NameListCompute, AnAddedNameIsHeldWhateverItsCase)
{
	std::vector<std::string> names;
	ComputeNameListAdd(names, "TstBlack");
	EXPECT_TRUE(ComputeNameListHas(names, "TSTBLACK"));
	EXPECT_TRUE(ComputeNameListHas(names, "tstblack"));
	EXPECT_FALSE(ComputeNameListHas(names, "TSTBLAC"));
	EXPECT_FALSE(ComputeNameListHas(names, "TSTBLACKS"));
	EXPECT_FALSE(ComputeNameListHas(names, "TSTWHITE"));
}

TEST(NameListCompute, TheEmptyNameIsANameLikeAnyOther)
{
	std::vector<std::string> names;
	ComputeNameListAdd(names, "");
	EXPECT_TRUE(ComputeNameListHas(names, ""));
	EXPECT_FALSE(ComputeNameListHas(names, "A"));
}

TEST(NameListCompute, ANameIsAddedOnce)
{
	std::vector<std::string> names;
	ComputeNameListAdd(names, "ICON");
	ComputeNameListAdd(names, "icon");
	ComputeNameListAdd(names, "BAR");
	EXPECT_EQ(names.size(), 2u);
}

TEST(NameListCompute, ANullNameIsNeverHeldOrAdded)
{
	std::vector<std::string> names;
	ComputeNameListAdd(names, nullptr);
	EXPECT_TRUE(names.empty());
	ComputeNameListAdd(names, "ICON");
	EXPECT_FALSE(ComputeNameListHas(names, nullptr));
}
