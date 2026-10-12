// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/computation/mirrorview_compute.h"

#include <gtest/gtest.h>

using namespace zx::visibility;

namespace {

const long long kUnit = 65536;

// [rc4l] A light-list boundary flat at height z, as secplane_t keeps a ceiling-facing one: z = (d + a x + b y) * ic / -2^32.
SheenPlane Flat(double z) { return { 0, 0, z * kUnit, -double(kUnit), false }; }

// [rc4l] One sloping along x, from z0 at x = 0 by `slope` a unit.
SheenPlane Sloped(double z0, double slope) { return { slope * kUnit, 0, z0 * kUnit, -double(kUnit), true }; }

SheenWall Wall(float x1, float x2, float bottom, float top)
{
	SheenWall w = { x1, 0.f, x2, 0.f, { top, top }, { bottom, bottom } };
	return w;
}

} // namespace

TEST(MirrorView, AStraightMirrorReflectsTheEyeHalfAUnitBeyondIt)
{
	long long x, y;
	ComputeMirrorEye(100 * kUnit, 0, 100 * kUnit, 200 * kUnit, 40 * kUnit, 50 * kUnit, 1, x, y);   // [rc4l] the line x = 100
	EXPECT_EQ(x, 160 * kUnit - kUnit / 2);
	EXPECT_EQ(y, 50 * kUnit);
	ComputeMirrorEye(100 * kUnit, 0, 100 * kUnit, 200 * kUnit, 140 * kUnit, 50 * kUnit, 1, x, y);
	EXPECT_EQ(x, 60 * kUnit + kUnit / 2);
	ComputeMirrorEye(0, 100 * kUnit, 200 * kUnit, 100 * kUnit, 50 * kUnit, 40 * kUnit, 1, x, y);   // [rc4l] the line y = 100
	EXPECT_EQ(x, 50 * kUnit);
	EXPECT_EQ(y, 160 * kUnit - kUnit / 2);
	ComputeMirrorEye(0, 100 * kUnit, 200 * kUnit, 100 * kUnit, 50 * kUnit, 140 * kUnit, 1, x, y);
	EXPECT_EQ(y, 60 * kUnit + kUnit / 2);
}

TEST(MirrorView, ASlantedMirrorReflectsAndNudgesAlongItsNormal)
{
	long long x, y;
	// [rc4l] The line y = x reflects (100, 0) to (0, 100), nudged by half the unit normal (-0.707, 0.707) turned about.
	ComputeMirrorEye(0, 0, 100 * kUnit, 100 * kUnit, 100 * kUnit, 0, 1, x, y);
	EXPECT_NEAR(x / 65536.0, 0.35355, 1e-4);
	EXPECT_NEAR(y / 65536.0, 100 - 0.35355, 1e-4);
	ComputeMirrorEye(0, 0, 100 * kUnit, 100 * kUnit, 100 * kUnit, 0, 2, x, y);   // [rc4l] a unit per depth
	EXPECT_NEAR(x / 65536.0, 0.70711, 1e-4);
}

TEST(MirrorView, AWallWithoutLightBandsTakesTheLastEntry)
{
	const SheenWall w = Wall(0, 256, 0, 128);
	EXPECT_EQ(ComputeSheenBand(w, NULL, 1), 0);
	const SheenWall point = Wall(16, 16, 0, 128);
	EXPECT_EQ(ComputeSheenBand(point, NULL, 1), kSheenNothing);   // [rc4l] SplitWall puts nothing for a wall of no length
}

TEST(MirrorView, TheBandOverTheWallsTopLightsItsFirstPiece)
{
	const SheenWall w = Wall(0, 256, 0, 128);
	const SheenPlane below[1] = { Flat(-10) };
	EXPECT_EQ(ComputeSheenBand(w, below, 2), kSheenOwnLight);   // [rc4l] the uppermost band holds the whole wall, in its own light
	const SheenPlane above[2] = { Flat(300), Flat(-10) };
	EXPECT_EQ(ComputeSheenBand(w, above, 3), 1);                // [rc4l] the first band is all above it, the second holds it
	const SheenPlane within[1] = { Flat(64) };
	EXPECT_EQ(ComputeSheenBand(w, within, 2), 0);               // [rc4l] the top part, put with the first band's light
	const SheenPlane allAbove[1] = { Flat(300) };
	EXPECT_EQ(ComputeSheenBand(w, allAbove, 2), 1);             // [rc4l] no band boundary in it: the last entry
}

TEST(MirrorView, ABandCrossingTheWallCutsItAndTheLeftPieceComesFirst)
{
	const SheenWall w = Wall(0, 256, 0, 128);
	// [rc4l] From 100 to 200 across the wall, through its top at 0.28 of the way; left of that the band is below the top.
	const SheenPlane up[1] = { Sloped(100, 100.0 / 256) };
	EXPECT_EQ(ComputeSheenBand(w, up, 2), 0);
	// [rc4l] From 64 to -36, through its bottom at 0.64 of the way: the left piece lies over it, the first band's.
	const SheenPlane down[1] = { Sloped(64, -100.0 / 256) };
	EXPECT_EQ(ComputeSheenBand(w, down, 2), 0);
}

TEST(MirrorView, ACrossingWithinATenthOfAnEndSnapsInsteadOfCutting)
{
	const SheenWall w = Wall(0, 256, 0, 128);
	// [rc4l] Just above the top at the left end and falling below it: snapped to the top there, so the top part goes with the band.
	const SheenPlane nearLeft[1] = { Sloped(128.01, -0.5) };
	EXPECT_EQ(ComputeSheenBand(w, nearLeft, 2), 0);
	// [rc4l] A wall of no height the band leaves just before its right end: snapped there, nothing is put.
	const SheenWall flat = Wall(0, 256, 64, 64);
	const SheenPlane nearRight[1] = { Sloped(70, -6.0 / 255.99) };
	EXPECT_EQ(ComputeSheenBand(flat, nearRight, 2), kSheenNothing);
}

TEST(MirrorView, ALeftPiecePuttingNothingLeavesTheFirstToTheRight)
{
	// [rc4l] A wall of no height at 64: the first boundary cuts it at 0.36, the second leaves the left piece just short of the cut.
	const SheenWall flat = Wall(0, 256, 64, 64);
	const SheenPlane planes[2] = { Sloped(100, -100.0 / 256), Sloped(70, -6.0 / 92.12) };
	EXPECT_EQ(ComputeSheenBand(flat, planes, 3), kSheenOwnLight);
}

TEST(MirrorView, ACrossingSnappedAtTheFarEndLeavesTheWallToTheNextBand)
{
	// [rc4l] Above the top at the left and just under it at the right end: snapped there, it holds no piece, and the next band holds the wall.
	const SheenWall w = Wall(0, 256, 0, 128);
	const SheenPlane planes[2] = { Sloped(130, -2.0001 / 256), Flat(-10) };
	EXPECT_EQ(ComputeSheenBand(w, planes, 3), 1);
}

TEST(ComputeSheenBandLight, TheBandsLevelUnlessTheWallsAndFullUnderAFadeWall)
{
	EXPECT_EQ(120, zx::visibility::ComputeSheenBandLight(120, false, 200, false));
	EXPECT_EQ(200, zx::visibility::ComputeSheenBandLight(120, true, 200, false));
	EXPECT_EQ(255, zx::visibility::ComputeSheenBandLight(120, true, 200, true));
	EXPECT_EQ(255, zx::visibility::ComputeSheenBandLight(120, false, 200, true));
}
