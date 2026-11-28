// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "actorext_compute.h"

#include <gtest/gtest.h>

using namespace zx::actorlayout;

TEST(ActorExt, AnActorOffsetIsNotABlockMember)
{
	EXPECT_FALSE(ComputeIsExtMember(0));
	EXPECT_FALSE(ComputeIsExtMember(1352));
	EXPECT_FALSE(ComputeIsExtMember(kExtMemberBase - 1));
}

TEST(ActorExt, ABlockMemberCodeRoundTripsToItsOffsetInTheBlock)
{
	for (size_t inBlock : { (size_t)0, (size_t)4, (size_t)200 })
	{
		const intptr_t code = ComputeExtMemberCode(inBlock);
		EXPECT_TRUE(ComputeIsExtMember(code));
		EXPECT_EQ(ComputeExtMemberOffset(code), inBlock);
	}
}
