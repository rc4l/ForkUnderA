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

// ---- ComputeLightBandIndex: degenerate inputs ------------------------------

TEST(LightBand, NoBandsReadsAsTheTopBand)
{
	EXPECT_EQ(ComputeLightBandIndex(0, 0, 0.0f, 0.0f, 0.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(0, 5, 0.0f, 0.0f, 0.0f), 0);

	const BandPlane one = Flat(0.0f);
	EXPECT_EQ(ComputeLightBandIndex(&one, 0, 0.0f, 0.0f, 0.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(&one, -3, 0.0f, 0.0f, 0.0f), 0);
}

TEST(LightBand, SingleBandAlwaysWins)
{
	const BandPlane one = Flat(100.0f);
	EXPECT_EQ(ComputeLightBandIndex(&one, 1, 0.0f, 0.0f, 500.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(&one, 1, 0.0f, 0.0f, -500.0f), 0);
}

// ---- ComputeLightBandIndex: the ordinary case ------------------------------

TEST(LightBand, PointPicksTheBandItSitsIn)
{
	// Bands top to bottom: [128..64) = 0, [64..0) = 1, below 0 = 2.
	const BandPlane planes[3] = { Flat(128.0f), Flat(64.0f), Flat(0.0f) };

	EXPECT_EQ(ComputeLightBandIndex(planes, 3, 0.0f, 0.0f, 200.0f), 0); // above everything
	EXPECT_EQ(ComputeLightBandIndex(planes, 3, 0.0f, 0.0f, 100.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(planes, 3, 0.0f, 0.0f, 65.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(planes, 3, 0.0f, 0.0f, 32.0f), 1);
	EXPECT_EQ(ComputeLightBandIndex(planes, 3, 0.0f, 0.0f, 1.0f), 1);
	EXPECT_EQ(ComputeLightBandIndex(planes, 3, 0.0f, 0.0f, -50.0f), 2); // below everything
}

TEST(LightBand, BoundaryBelongsToTheLowerBand)
{
	// SplitWall ends a band at the next plane, so a pixel exactly on that plane is already the
	// band below it. Getting this backwards would shift every band seam by one texel.
	const BandPlane planes[2] = { Flat(128.0f), Flat(64.0f) };
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 0.0f, 0.0f, 64.0f), 1);
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 0.0f, 0.0f, 64.001f), 0);
}

TEST(LightBand, TopPlaneItselfDoesNotBoundTheTopBand)
{
	// Only plane[i+1] ends band i, so plane[0]'s own height never selects a band -- a point far
	// above the whole stack is still band 0.
	const BandPlane planes[2] = { Flat(10.0f), Flat(0.0f) };
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 0.0f, 0.0f, 10.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 0.0f, 0.0f, 99999.0f), 0);
}

TEST(LightBand, SlopedBandsSelectByPositionNotJustHeight)
{
	// This is the case that forces the geometry split today: one band boundary crossing a wall
	// diagonally. Per-fragment the same height lands in different bands at different x.
	const BandPlane planes[2] = { Flat(1000.0f), SlopedX(0.0f, 10.0f) };

	// boundary height is 10*x, so at height 50: above the slope for x<5, below for x>5.
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 0.0f, 0.0f, 50.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 4.0f, 0.0f, 50.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 6.0f, 0.0f, 50.0f), 1);
	EXPECT_EQ(ComputeLightBandIndex(planes, 2, 100.0f, 0.0f, 50.0f), 1);
}

TEST(LightBand, DeepStackResolvesEveryBand)
{
	BandPlane planes[8];
	for (int i = 0; i < 8; i++) planes[i] = Flat(700.0f - 100.0f * i); // 700, 600, ... 0

	for (int i = 0; i < 7; i++)
	{
		const float mid = 650.0f - 100.0f * i;
		EXPECT_EQ(ComputeLightBandIndex(planes, 8, 0.0f, 0.0f, mid), i) << "band " << i;
	}
	EXPECT_EQ(ComputeLightBandIndex(planes, 8, 0.0f, 0.0f, -10.0f), 7);
}

TEST(LightBand, CoincidentPlanesCollapseToTheLowestOfThem)
{
	// P_Recalculate3DFloors can leave zero-thickness bands; every one of them must resolve to a
	// single deterministic index rather than to whichever the loop happened to see first.
	const BandPlane planes[4] = { Flat(64.0f), Flat(32.0f), Flat(32.0f), Flat(0.0f) };
	EXPECT_EQ(ComputeLightBandIndex(planes, 4, 0.0f, 0.0f, 40.0f), 0);
	EXPECT_EQ(ComputeLightBandIndex(planes, 4, 0.0f, 0.0f, 32.0f), 2);
	EXPECT_EQ(ComputeLightBandIndex(planes, 4, 0.0f, 0.0f, 16.0f), 2);
}

// ---- Band count limits -----------------------------------------------------

TEST(LightBand, UploadableCountClampsToTheShaderArray)
{
	EXPECT_EQ(ComputeUploadableBandCount(0), 0);
	EXPECT_EQ(ComputeUploadableBandCount(1), 1);
	EXPECT_EQ(ComputeUploadableBandCount(kMaxLightBands), kMaxLightBands);
	EXPECT_EQ(ComputeUploadableBandCount(kMaxLightBands + 1), kMaxLightBands);
	EXPECT_EQ(ComputeUploadableBandCount(9999), kMaxLightBands);
}

TEST(LightBand, UploadableCountIsNeverNegative)
{
	EXPECT_EQ(ComputeUploadableBandCount(-1), 0);
	EXPECT_EQ(ComputeUploadableBandCount(-9999), 0);
}

TEST(LightBand, GeometrySplitOnlyBeyondTheArrayLimit)
{
	EXPECT_FALSE(ComputeNeedsGeometrySplit(0));
	EXPECT_FALSE(ComputeNeedsGeometrySplit(1));
	EXPECT_FALSE(ComputeNeedsGeometrySplit(kMaxLightBands));
	EXPECT_TRUE(ComputeNeedsGeometrySplit(kMaxLightBands + 1));
}
