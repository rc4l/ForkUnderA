// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/sprites/computation/weaponease_compute.h"

#include <gtest/gtest.h>

using namespace zx::sprites;

namespace {

const int64_t kUnit = 65536;

struct Drawn { int64_t x, y; };

// [rc4l] The body every case's layers sit on, and the one a respawn brings.
const int kBodies[2] = { 0, 0 };
const void *const kBody = &kBodies[0];
const void *const kNewBody = &kBodies[1];

Drawn Draw(WeaponEase &e, bool on, int tic, int64_t x, int64_t y, int64_t frac, const void *owner = kBody)
{
	Drawn d;
	ComputeWeaponEase(e, on, tic, owner, x, y, frac, &d.x, &d.y);
	return d;
}

} // namespace

TEST(WeaponEase, FirstFrameDrawsWhereTheLayerIs)
{
	WeaponEase e = {};
	const Drawn d = Draw(e, true, 10, 5 * kUnit, 7 * kUnit, kUnit / 2);
	EXPECT_EQ(d.x, 5 * kUnit);
	EXPECT_EQ(d.y, 7 * kUnit);
	EXPECT_TRUE(e.seen);
}

TEST(WeaponEase, ANewTicEasesFromLastTicsPosition)
{
	WeaponEase e = {};
	Draw(e, true, 10, 0, 0, 0);
	const Drawn half = Draw(e, true, 11, 10 * kUnit, -4 * kUnit, kUnit / 2);
	EXPECT_EQ(half.x, 5 * kUnit);
	EXPECT_EQ(half.y, -2 * kUnit);
	// [rc4l] A later frame of the same tic keeps easing from the same start.
	const Drawn whole = Draw(e, true, 11, 10 * kUnit, -4 * kUnit, kUnit);
	EXPECT_EQ(whole.x, 10 * kUnit);
	EXPECT_EQ(whole.y, -4 * kUnit);
}

TEST(WeaponEase, ASecondFrameInOneTicIsIdempotent)
{
	WeaponEase e = {};
	Draw(e, true, 1, 0, 0, 0);
	const Drawn a = Draw(e, true, 2, 8 * kUnit, 0, kUnit / 4);
	const Drawn b = Draw(e, true, 2, 8 * kUnit, 0, kUnit / 4);
	EXPECT_EQ(a.x, b.x);
	EXPECT_EQ(a.x, 2 * kUnit);
}

TEST(WeaponEase, OffDrawsWhereItIsAndForgetsTheLastTic)
{
	WeaponEase e = {};
	Draw(e, true, 1, 0, 0, 0);
	const Drawn off = Draw(e, false, 2, 8 * kUnit, 3 * kUnit, kUnit / 2);
	EXPECT_EQ(off.x, 8 * kUnit);
	EXPECT_EQ(off.y, 3 * kUnit);
	EXPECT_FALSE(e.seen);
	// [rc4l] Turned back on, the first frame starts fresh rather than easing from before it was off.
	const Drawn on = Draw(e, true, 3, 12 * kUnit, 3 * kUnit, kUnit / 2);
	EXPECT_EQ(on.x, 12 * kUnit);
}

TEST(WeaponEase, ATicBackwardsOrALongGapSnaps)
{
	WeaponEase e = {};
	Draw(e, true, 100, 0, 0, 0);
	EXPECT_EQ(Draw(e, true, 5, 6 * kUnit, 0, kUnit / 2).x, 6 * kUnit);
	EXPECT_EQ(Draw(e, true, 5 + 36, 20 * kUnit, 0, kUnit / 2).x, 20 * kUnit);
	// [rc4l] A gap of up to a second still eases.
	EXPECT_EQ(Draw(e, true, 5 + 36 + 35, 30 * kUnit, 0, kUnit / 2).x, 25 * kUnit);
}

TEST(WeaponEase, ANewBodySnapsAndThenEases)
{
	// [rc4l] The tic after a respawn draws the new layers where they are, not slid from where the old body's stood.
	WeaponEase e = {};
	Draw(e, true, 10, 40 * kUnit, 90 * kUnit, 0);
	const Drawn respawned = Draw(e, true, 11, 0, 100 * kUnit, kUnit / 2, kNewBody);
	EXPECT_EQ(respawned.x, 0);
	EXPECT_EQ(respawned.y, 100 * kUnit);
	EXPECT_EQ(e.owner, kNewBody);
	EXPECT_EQ(Draw(e, true, 12, 0, 80 * kUnit, kUnit / 2, kNewBody).y, 90 * kUnit);
}
