// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/rendersettings/computation/mapoptions_compute.h"

#include <gtest/gtest.h>

using namespace zx::rendersettings;

namespace {

const MapRenderOptions kMapLeavesAll = { -1, -1, -1, -1 };
const PlayerRenderSettings kPlayer = { 2, 3, true, true, false, false };

} // namespace

TEST(MapOptions, OnlyTheDrawnLightModesAreValid)
{
	for (int m : { 0, 1, 2, 3, 4, 8 }) EXPECT_TRUE(ComputeLightmodeValid(m)) << m;
	for (int m : { -1, 5, 7, 9 }) EXPECT_FALSE(ComputeLightmodeValid(m)) << m;
}

TEST(MapOptions, AMapThatSetsNothingTakesThePlayersSettings)
{
	const ResolvedRenderSettings r = ComputeMapRenderSettings(kMapLeavesAll, kPlayer);
	EXPECT_EQ(2, r.lightmode);
	EXPECT_TRUE(r.brightfog);
	EXPECT_TRUE(r.nocoloredspritelighting);
	EXPECT_FALSE(r.notexturefill);
}

TEST(MapOptions, TheMapsChoicesWin)
{
	const MapRenderOptions map = { 8, 0, 0, 1 };
	const ResolvedRenderSettings r = ComputeMapRenderSettings(map, kPlayer);
	EXPECT_EQ(8, r.lightmode);
	EXPECT_FALSE(r.brightfog);
	EXPECT_FALSE(r.nocoloredspritelighting);
	EXPECT_TRUE(r.notexturefill);
}

// [rc4l] A forced server reads the light mode's default where the map does not choose one.
TEST(MapOptions, ForcedDefaultsReplaceThePlayersLightMode)
{
	PlayerRenderSettings forced = kPlayer;
	forced.forced = true;
	EXPECT_EQ(3, ComputeMapRenderSettings(kMapLeavesAll, forced).lightmode);
	const MapRenderOptions map = { 4, -1, -1, -1 };
	EXPECT_EQ(4, ComputeMapRenderSettings(map, forced).lightmode);
}
