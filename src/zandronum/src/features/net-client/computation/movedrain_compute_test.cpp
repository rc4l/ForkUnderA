// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/movedrain_compute.h"

#include <gtest/gtest.h>

using namespace zx::netclient;

TEST(MoveDrain, ArrivalsCountTicsThatBroughtACommand)
{
	EXPECT_EQ(ComputeConsistentArrivals(4, 100, 100), 5u);
	EXPECT_EQ(ComputeConsistentArrivals(4, 99, 100), 0u);
	EXPECT_EQ(ComputeConsistentArrivals(0, 0, 0), 1u);
}

TEST(MoveDrain, NothingBufferedRunsNothing)
{
	EXPECT_TRUE(ComputeMoveDrainOrder({}, 35).empty());
	EXPECT_TRUE(ComputeMoveDrainOrder({ { true, 0, 0 }, { true, 0, 50 } }, 35).empty());
}

TEST(MoveDrain, EachPlayerRunsOneCommandFirst)
{
	EXPECT_EQ(ComputeMoveDrainOrder({ { true, 1, 0 }, { true, 1, 0 } }, 35), (std::vector<int>{ 0, 1 }));
}

TEST(MoveDrain, AnAbsentOrSpectatingPlayerRunsNothing)
{
	EXPECT_EQ(ComputeMoveDrainOrder({ { false, 3, 0 }, { true, 3, 0 } }, 35), (std::vector<int>{ 1, 1 }));
}

TEST(MoveDrain, ASecondRunsWhileAnotherWaitsBehindIt)
{
	EXPECT_EQ(ComputeMoveDrainOrder({ { true, 3, 0 }, { true, 2, 0 } }, 35), (std::vector<int>{ 0, 1, 0 }));
}

TEST(MoveDrain, TheLastWaitingRunsSecondOnlyAfterASecondOfSteadyArrivals)
{
	EXPECT_EQ(ComputeMoveDrainOrder({ { true, 2, 34 } }, 35), (std::vector<int>{ 0 }));
	EXPECT_EQ(ComputeMoveDrainOrder({ { true, 2, 35 } }, 35), (std::vector<int>{ 0, 0 }));
}

TEST(MoveDrain, ATickSavesWherePlayersWereUnlessAClientDrainsThem)
{
	EXPECT_TRUE(ComputeTickSavesPrevPosition(false, true, true));
	EXPECT_TRUE(ComputeTickSavesPrevPosition(true, false, true));
	EXPECT_TRUE(ComputeTickSavesPrevPosition(true, true, false));
	EXPECT_FALSE(ComputeTickSavesPrevPosition(true, true, true));
}

TEST(MoveDrain, ADemosPlaybackBuffersAsItsRecorderDid)
{
	EXPECT_TRUE(ComputeUsesCommandBuffers(true, true, false));
	EXPECT_FALSE(ComputeUsesCommandBuffers(true, false, true));
	EXPECT_TRUE(ComputeUsesCommandBuffers(false, false, true));
	EXPECT_FALSE(ComputeUsesCommandBuffers(false, true, false));
}

TEST(MoveDrain, ACommandWaitsOnlyWhileTheBuffersAreNotBeingDrained)
{
	EXPECT_TRUE(ComputeBuffersCommand(false, true));
	EXPECT_FALSE(ComputeBuffersCommand(true, true));
	EXPECT_FALSE(ComputeBuffersCommand(false, false));
}
