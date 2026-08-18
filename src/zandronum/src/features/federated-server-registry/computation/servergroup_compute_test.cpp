// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/federated-server-registry/computation/servergroup_compute.h"

using namespace zx;

TEST(ServerGroup, OneOfEachFamilyOnOnePortIsOneServer)
{
	// The only shape a dual-stack server can produce: one socket, one port, an announce per family.
	EXPECT_EQ(GroupVerdict::Group, DecideServerGroup(1, 1, true));
	EXPECT_TRUE(ShouldSendGroup(DecideServerGroup(1, 1, true)));
}

TEST(ServerGroup, OneAddressIsNotAGroup)
{
	// Every ordinary server. Sending a group of one would be a packet per server for nothing.
	EXPECT_EQ(GroupVerdict::Alone, DecideServerGroup(1, 0, true));
	EXPECT_EQ(GroupVerdict::Alone, DecideServerGroup(0, 1, true));
	EXPECT_FALSE(ShouldSendGroup(DecideServerGroup(1, 0, true)));
}

TEST(ServerGroup, NothingAtAllIsNotAGroup)
{
	EXPECT_EQ(GroupVerdict::Alone, DecideServerGroup(0, 0, true));
}

TEST(ServerGroup, TwoOfOneFamilyIsACollision)
{
	// One socket cannot announce twice over the same family, so these are two servers that arrived at
	// one identity -- which happens for real when a machine image is copied, key file and all.
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(2, 0, true));
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(0, 2, true));
	EXPECT_FALSE(ShouldSendGroup(DecideServerGroup(2, 0, true)));
}

TEST(ServerGroup, MoreThanTwoIsACollision)
{
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(2, 1, true));
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(1, 2, true));
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(9, 9, true));
}

TEST(ServerGroup, TheRightFamiliesOnDifferentPortsIsRefusedToo)
{
	// Also impossible from one socket.
	EXPECT_EQ(GroupVerdict::PortMismatch, DecideServerGroup(1, 1, false));
	EXPECT_FALSE(ShouldSendGroup(DecideServerGroup(1, 1, false)));
}

TEST(ServerGroup, ImpossibleCountsReadAsACollisionRatherThanAGroup)
{
	// A caller that has gone wrong must not be answered with "merge these".
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(-1, 1, true));
	EXPECT_EQ(GroupVerdict::Collision, DecideServerGroup(1, -1, true));
}

TEST(ServerGroup, OnlyTheImpossibleShapesAreWorthTellingTheOperator)
{
	// A registry logging a line per server per query drowns the one message that matters, and the one
	// that matters is "somebody's server is being hidden".
	EXPECT_TRUE(GroupNeedsReport(GroupVerdict::Collision));
	EXPECT_TRUE(GroupNeedsReport(GroupVerdict::PortMismatch));
	EXPECT_FALSE(GroupNeedsReport(GroupVerdict::Group));
	EXPECT_FALSE(GroupNeedsReport(GroupVerdict::Alone));
}

TEST(ServerGroup, TheFailureDirectionIsAlwaysTowardsShowingBothRows)
{
	// The property that matters more than any single case: nothing but the exact dual-stack shape is
	// ever merged.
	for (int v4 = 0; v4 <= 3; ++v4)
	{
		for (int v6 = 0; v6 <= 3; ++v6)
		{
			for (int port = 0; port <= 1; ++port)
			{
				const bool samePort = (port == 1);
				const bool expected = (v4 == 1) && (v6 == 1) && samePort;

				EXPECT_EQ(expected, ShouldSendGroup(DecideServerGroup(v4, v6, samePort)))
					<< "v4=" << v4 << " v6=" << v6 << " samePort=" << samePort;
			}
		}
	}
}
