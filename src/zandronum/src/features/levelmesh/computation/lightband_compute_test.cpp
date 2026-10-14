// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/lightband_compute.h"

using namespace zx::levelmesh;

// A flat plane at height z, packed the way secplane_t stores a horizontal floor: a=b=0, c=1, ic=1,
// d=-z, so ZatPoint = ic * (-d) = z.
static BandPlane Flat(float z)
{
	BandPlane p = { 0.0f, 0.0f, 1.0f, -z };
	return p;
}

// A plane sloping along +x: height is baseZ at x=0 and rises by `slope` per unit x.
// ZatPoint = ic*(-d - a*x) must equal baseZ + slope*x, so with ic=1: a = -slope, d = -baseZ.
static BandPlane SlopedX(float baseZ, float slope)
{
	BandPlane p = { -slope, 0.0f, 1.0f, -baseZ };
	return p;
}

// ---- ComputeBandPlaneZ -----------------------------------------------------

TEST(LightBand, FlatPlaneHeightIsConstant)
{
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(Flat(64.0f), 0.0f, 0.0f), 64.0f);
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(Flat(64.0f), 900.0f, -400.0f), 64.0f);
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(Flat(-32.0f), 12.0f, 7.0f), -32.0f);
}

TEST(LightBand, SlopedPlaneHeightTracksPosition)
{
	const BandPlane p = SlopedX(10.0f, 2.0f);
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(p, 0.0f, 0.0f), 10.0f);
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(p, 5.0f, 0.0f), 20.0f);
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(p, -5.0f, 0.0f), 0.0f);
}

TEST(LightBand, PlaneHeightMatchesSecplaneFormula)
{
	// Arbitrary non-degenerate plane; the expectation is written out longhand as secplane_t would.
	const BandPlane p = { 0.25f, -0.5f, 2.0f, 8.0f };
	const float x = 3.0f, y = -6.0f;
	const float expected = 2.0f * (-8.0f - 0.25f * x - (-0.5f) * y);
	EXPECT_FLOAT_EQ(ComputeBandPlaneZ(p, x, y), expected);
}

// [rc4l] wdiexsec's pool wall took the water's blue light above the water: the bake read the -80 surface as -0.001, a fixed height counted twice.
TEST(LightBand, AFixedPlaneConvertsToItsHeightInMapUnits)
{
	// [rc4l] A 3D floor's top is its control sector's ceiling: c = -1, so ic = -1 and d = the height, all in 16.16.
	const BandPlane ceiling = ComputeBandPlaneFromFixed(0.0, 0.0, -65536.0, -80.0 * 65536.0);
	EXPECT_FLOAT_EQ(-80.f, ComputeBandPlaneZ(ceiling, 512.f, 896.f));
	// [rc4l] A floor at -154: c = 1, so ic = 1 and d = 154.
	const BandPlane floor = ComputeBandPlaneFromFixed(0.0, 0.0, 65536.0, 154.0 * 65536.0);
	EXPECT_FLOAT_EQ(-154.f, ComputeBandPlaneZ(floor, -2000.f, 3000.f));
}

TEST(LightBand, AFlatBoundaryIsReadAtTheSecondEndForBoth)
{
	const BandPlane water = ComputeBandPlaneFromFixed(0.0, 0.0, -65536.0, -80.0 * 65536.0);
	float out[2];
	ComputeBandBottomAtEnds(water, 256.f, 896.f, 768.f, 896.f, out);
	EXPECT_FLOAT_EQ(-80.f, out[0]);
	EXPECT_FLOAT_EQ(-80.f, out[1]);
}

TEST(LightBand, ASlopedBoundaryIsReadAtEachEnd)
{
	// [rc4l] Rising one unit per eight east: a = -0.125 in 16.16 with c = 1 makes z = 0.125 * x.
	const BandPlane slope = ComputeBandPlaneFromFixed(-0.125 * 65536.0, 0.0, 65536.0, 0.0);
	float out[2];
	ComputeBandBottomAtEnds(slope, 0.f, 0.f, 64.f, 0.f, out);
	EXPECT_FLOAT_EQ(0.f, out[0]);
	EXPECT_FLOAT_EQ(8.f, out[1]);
}
