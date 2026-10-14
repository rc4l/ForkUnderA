// [MobiusU9B] Tests for the telemeter's rules, covering every branch of telemeter_compute.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/crosshair/computation/telemeter_compute.h"

#include <gtest/gtest.h>

#include <limits>

using zx::crosshair::ComputeSweepRange;
using zx::crosshair::ComputeTelemeterActive;
using zx::crosshair::ComputeTelemeterGap;
using zx::crosshair::ComputeTelemeterMeasures;
using zx::crosshair::ComputeTelemeterText;
using zx::crosshair::kIdentifyRange;
using zx::crosshair::kSweepDecoration;
using zx::crosshair::kSweepItem;
using zx::crosshair::kSweepMonster;
using zx::crosshair::kSweepPlayer;
using zx::crosshair::kSweepProjectile;
using zx::crosshair::kTelemeterRange;
using zx::crosshair::SweepEntry;
using zx::crosshair::TelemeterToggles;

namespace
{
	const double kNaN = std::numeric_limits<double>::quiet_NaN();

	// [MobiusU9B] The defaults once the telemeter is switched on: every kind measured.
	const TelemeterToggles kAllOn{true, true, true, true, true};

	// [MobiusU9B] Switched on, but no kind left to measure.
	const TelemeterToggles kNoKinds{true, false, false, false, false};
}

TEST(ComputeTelemeterActive, NeedsTheSwitchAndAKind)
{
	EXPECT_TRUE(ComputeTelemeterActive(kAllOn));
	EXPECT_FALSE(ComputeTelemeterActive(TelemeterToggles{false, true, true, true, true}));
	EXPECT_FALSE(ComputeTelemeterActive(kNoKinds));
}

TEST(ComputeTelemeterActive, AnyOneKindIsEnough)
{
	EXPECT_TRUE(ComputeTelemeterActive(TelemeterToggles{true, true, false, false, false}));
	EXPECT_TRUE(ComputeTelemeterActive(TelemeterToggles{true, false, true, false, false}));
	EXPECT_TRUE(ComputeTelemeterActive(TelemeterToggles{true, false, false, true, false}));
	EXPECT_TRUE(ComputeTelemeterActive(TelemeterToggles{true, false, false, false, true}));
}

TEST(ComputeTelemeterMeasures, MeasuresEachKindWhoseToggleIsOn)
{
	EXPECT_TRUE(ComputeTelemeterMeasures(TelemeterToggles{true, true, false, false, false}, kSweepDecoration, false));
	EXPECT_TRUE(ComputeTelemeterMeasures(TelemeterToggles{true, false, true, false, false}, kSweepMonster, false));
	EXPECT_TRUE(ComputeTelemeterMeasures(TelemeterToggles{true, false, false, true, false}, kSweepPlayer, false));
	EXPECT_TRUE(ComputeTelemeterMeasures(TelemeterToggles{true, false, false, false, true}, kSweepProjectile, false));
}

TEST(ComputeTelemeterMeasures, LooksPastEachKindWhoseToggleIsOff)
{
	EXPECT_FALSE(ComputeTelemeterMeasures(TelemeterToggles{true, false, true, true, true}, kSweepDecoration, false));
	EXPECT_FALSE(ComputeTelemeterMeasures(TelemeterToggles{true, true, false, true, true}, kSweepMonster, false));
	EXPECT_FALSE(ComputeTelemeterMeasures(TelemeterToggles{true, true, true, false, true}, kSweepPlayer, false));
	EXPECT_FALSE(ComputeTelemeterMeasures(TelemeterToggles{true, true, true, true, false}, kSweepProjectile, false));
}

TEST(ComputeTelemeterMeasures, NeverMeasuresItems)
{
	EXPECT_FALSE(ComputeTelemeterMeasures(kAllOn, kSweepItem, false));
}

TEST(ComputeTelemeterMeasures, MeasuresNothingWhileInactive)
{
	EXPECT_FALSE(ComputeTelemeterMeasures(TelemeterToggles{false, true, true, true, true}, kSweepMonster, true));
	EXPECT_FALSE(ComputeTelemeterMeasures(kNoKinds, kSweepMonster, true));
}

TEST(ComputeTelemeterMeasures, MeasuresEveryKindWhenTheServerHidesThem)
{
	// [MobiusU9B] On sv_noidentifytarget servers, so a player disguised as a prop reads like the real props around it.
	const TelemeterToggles decorationsOnly{true, true, false, false, false};
	EXPECT_TRUE(ComputeTelemeterMeasures(decorationsOnly, kSweepPlayer, true));
	EXPECT_TRUE(ComputeTelemeterMeasures(decorationsOnly, kSweepItem, true));
}

TEST(ComputeSweepRange, ReachesFurtherForTheTelemeter)
{
	EXPECT_EQ(kTelemeterRange, ComputeSweepRange(true));
	EXPECT_EQ(kIdentifyRange, ComputeSweepRange(false));
}

TEST(ComputeTelemeterGap, MeasuresBetweenTheTwoBodies)
{
	// [MobiusU9B] The line leaves a Doom player's body 16 units out and enters the target's 460 units out.
	EXPECT_DOUBLE_EQ(444.0, ComputeTelemeterGap(SweepEntry{true, 460.0}, 460.0, 16.0));
}

TEST(ComputeTelemeterGap, ReadsZeroWhenTheBodiesTouchOrOverlap)
{
	EXPECT_DOUBLE_EQ(0.0, ComputeTelemeterGap(SweepEntry{true, 16.0}, 16.0, 16.0));
	EXPECT_DOUBLE_EQ(0.0, ComputeTelemeterGap(SweepEntry{true, 10.0}, 10.0, 16.0));
	EXPECT_DOUBLE_EQ(0.0, ComputeTelemeterGap(SweepEntry{true, kNaN}, 10.0, 16.0));
}

TEST(ComputeTelemeterGap, FallsBackToAWidenedAttackBoxWhenTheLineMissesTheBody)
{
	EXPECT_DOUBLE_EQ(84.0, ComputeTelemeterGap(SweepEntry{false, 0.0}, 100.0, 16.0));
}

TEST(ComputeTelemeterText, RoundsToWholeMapUnits)
{
	EXPECT_EQ("384 u", ComputeTelemeterText(384.0));
	EXPECT_EQ("384 u", ComputeTelemeterText(383.5));
	EXPECT_EQ("383 u", ComputeTelemeterText(383.49));
	EXPECT_EQ("0 u", ComputeTelemeterText(0.2));
}

TEST(ComputeTelemeterText, ReadsZeroForNothingSensible)
{
	EXPECT_EQ("0 u", ComputeTelemeterText(0.0));
	EXPECT_EQ("0 u", ComputeTelemeterText(-5.0));
	EXPECT_EQ("0 u", ComputeTelemeterText(kNaN));
}

TEST(ComputeTelemeterText, NeverReadsPastTheRange)
{
	EXPECT_EQ("8192 u", ComputeTelemeterText(8192.0));
	EXPECT_EQ("8192 u", ComputeTelemeterText(1e30));
	EXPECT_EQ("8192 u", ComputeTelemeterText(std::numeric_limits<double>::infinity()));
}
