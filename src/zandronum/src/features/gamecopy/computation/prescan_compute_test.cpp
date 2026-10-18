// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/computation/prescan_compute.h"

#include <gtest/gtest.h>

using namespace zx::gamecopy;

namespace {

const PrescanStamp kNow = { true, 41, 3, 7, 900, 1200, 6200, 3100 };

}

TEST(PrescanCompute, AScanFoundForThisFillHolds)
{
	EXPECT_TRUE(ComputePrescanFresh(kNow, kNow, false));
}

// [rc4l] Each way the map can have changed since the tic's end turns the handoff back to the whole scan.
TEST(PrescanCompute, AnyChangeSinceTheScanVoidsIt)
{
	PrescanStamp s = kNow;
	s.found = false;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false)) << "a tic that ran no playsim found nothing";
	s = kNow; s.fill = 40;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false)) << "found for an earlier fill";
	s = kNow; s.level = 2;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false)) << "found on the last level";
	s = kNow; s.effects = 6;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false)) << "a main-thread effect ran since";
	s = kNow; s.sectors = 1199;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false));
	s = kNow; s.sides = 6199;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false));
	s = kNow; s.lines = 3099;
	EXPECT_FALSE(ComputePrescanFresh(s, kNow, false));
	EXPECT_FALSE(ComputePrescanFresh(kNow, kNow, true)) << "a client's map changes between tics";
}

// [rc4l] A medal icon spawned after the playsim joins the thinker and sector lists the scan walked, but changes nothing it compared on the map.
TEST(PrescanCompute, AListChangeVoidsTheActorScanAlone)
{
	PrescanStamp s = kNow;
	EXPECT_TRUE(ComputeActorScanFresh(s, kNow, false));
	s.lists = 899;
	EXPECT_FALSE(ComputeActorScanFresh(s, kNow, false));
	EXPECT_TRUE(ComputePrescanFresh(s, kNow, false));
	s = kNow; s.fill = 40;
	EXPECT_FALSE(ComputeActorScanFresh(s, kNow, false));
}

TEST(PrescanCompute, TheHandoffTakesEachCandidateOnceInOrder)
{
	std::vector<int> out = { 99 };
	ComputeTakeCandidates({ 40, 3, 17 }, { 17, 5 }, { 3, 2 }, 100, out);
	EXPECT_EQ((std::vector<int>{ 2, 3, 5, 17, 40 }), out);
}

// [rc4l] A blend or seen mark left from a level with more items names none of this one's.
TEST(PrescanCompute, CandidatesOutsideTheMapAreDropped)
{
	std::vector<int> out;
	ComputeTakeCandidates({ 4, -1 }, { 10, 12 }, {}, 10, out);
	EXPECT_EQ((std::vector<int>{ 4 }), out);
	ComputeTakeCandidates({}, {}, {}, 10, out);
	EXPECT_TRUE(out.empty());
}
