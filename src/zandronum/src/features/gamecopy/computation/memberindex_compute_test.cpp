// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/memberindex_compute.h"

#include <gtest/gtest.h>

using zx::gamecopy::ComputeMemberIndex;

namespace {
struct Thing { int a; short light; double z; };
}

TEST(MemberIndex, FindsTheElementAMemberBelongsTo)
{
	Thing things[4] = {};
	const size_t offset = size_t(reinterpret_cast<char *>(&things[0].light) - reinterpret_cast<char *>(&things[0]));
	EXPECT_EQ(0, ComputeMemberIndex(things, sizeof(Thing), 4, offset, &things[0].light));
	EXPECT_EQ(3, ComputeMemberIndex(things, sizeof(Thing), 4, offset, &things[3].light));
}

TEST(MemberIndex, AnotherMemberIsNone)
{
	Thing things[4] = {};
	const size_t offset = size_t(reinterpret_cast<char *>(&things[0].light) - reinterpret_cast<char *>(&things[0]));
	EXPECT_EQ(-1, ComputeMemberIndex(things, sizeof(Thing), 4, offset, &things[2].z));
	EXPECT_EQ(-1, ComputeMemberIndex(things, sizeof(Thing), 4, offset, &things[0].a));
}

TEST(MemberIndex, OutsideTheArrayIsNone)
{
	Thing things[4] = {};
	const size_t offset = size_t(reinterpret_cast<char *>(&things[0].light) - reinterpret_cast<char *>(&things[0]));
	EXPECT_EQ(-1, ComputeMemberIndex(things, sizeof(Thing), 3, offset, &things[3].light));
	EXPECT_EQ(-1, ComputeMemberIndex(&things[1], sizeof(Thing), 3, offset, &things[0].light));
}

TEST(MemberIndex, NothingToLookIn)
{
	Thing things[1] = {};
	EXPECT_EQ(-1, ComputeMemberIndex(nullptr, sizeof(Thing), 1, 0, &things[0]));
	EXPECT_EQ(-1, ComputeMemberIndex(things, sizeof(Thing), 1, 0, nullptr));
	EXPECT_EQ(-1, ComputeMemberIndex(things, 0, 1, 0, &things[0]));
	EXPECT_EQ(-1, ComputeMemberIndex(things, sizeof(Thing), 0, 0, &things[0]));
}
