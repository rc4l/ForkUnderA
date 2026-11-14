// [Doruk-Aksoy] Tests for spotlight_compute, each expected value what UZDoom's p_mobj.cpp, AddLightToList and spotLightAttenuation give.
#include <gtest/gtest.h>

#include <cmath>

#include "features/dynlights/computation/spotlight_compute.h"

using namespace zx::dynlights;

namespace {

const float kCos10 = float(std::cos(10.0 * 3.14159265358979323846 / 180.0));
const float kCos25 = float(std::cos(25.0 * 3.14159265358979323846 / 180.0));

}

// [Doruk-Aksoy] A spot's arg0 is the colour packed 0xRRGGBB, arg1 and arg2 the cone's angles.
TEST(ComputeMapLightArgs, SpotUnpacksColourAndCone)
{
	const MapLightArgs a = ComputeMapLightArgs(0x4D80FF, 10, 25, true, -1);
	EXPECT_EQ(a.red, 0x4D);
	EXPECT_EQ(a.green, 0x80);
	EXPECT_EQ(a.blue, 0xFF);
	EXPECT_FLOAT_EQ(a.inner, 10.f);
	EXPECT_FLOAT_EQ(a.outer, 25.f);
	const MapLightArgs black = ComputeMapLightArgs(0, 0, 0, true, -1);
	EXPECT_EQ(black.red + black.green + black.blue, 0);
}

// [Doruk-Aksoy] Any other light keeps its args as red, green and blue, and has no cone.
TEST(ComputeMapLightArgs, PointLightKeepsItsArgs)
{
	const MapLightArgs a = ComputeMapLightArgs(77, 128, 255, false, -1);
	EXPECT_EQ(a.red, 77);
	EXPECT_EQ(a.green, 128);
	EXPECT_EQ(a.blue, 255);
	EXPECT_FLOAT_EQ(a.inner, 0.f);
	EXPECT_FLOAT_EQ(a.outer, 0.f);
}

// [Doruk-Aksoy] A named colour wins for any light, and a spot still takes its cone from the map's arg1 and arg2.
TEST(ComputeMapLightArgs, NamedColourWins)
{
	const MapLightArgs point = ComputeMapLightArgs(-1234, 0, 0, false, 0xFF8000);
	EXPECT_EQ(point.red, 0xFF);
	EXPECT_EQ(point.green, 0x80);
	EXPECT_EQ(point.blue, 0x00);
	const MapLightArgs spot = ComputeMapLightArgs(-1234, 15, 40, true, 0x000000);
	EXPECT_EQ(spot.red + spot.green + spot.blue, 0);
	EXPECT_FLOAT_EQ(spot.inner, 15.f);
	EXPECT_FLOAT_EQ(spot.outer, 40.f);
}

// [Doruk-Aksoy] The direction is the facing negated: a light facing east (yaw 0) points its vector west, one facing north (90) south.
TEST(ComputeSpotDirection, IsTheFacingNegated)
{
	float d[3];
	ComputeSpotDirection(0.0, 0.0, d);
	EXPECT_NEAR(d[0], -1.f, 1e-6f); EXPECT_NEAR(d[1], 0.f, 1e-6f); EXPECT_NEAR(d[2], 0.f, 1e-6f);
	ComputeSpotDirection(90.0, 0.0, d);
	EXPECT_NEAR(d[0], 0.f, 1e-6f); EXPECT_NEAR(d[1], -1.f, 1e-6f); EXPECT_NEAR(d[2], 0.f, 1e-6f);
}

// [Doruk-Aksoy] Pitch is positive down, so a light pitched down 90 points its vector up, and 45 splits it evenly.
TEST(ComputeSpotDirection, PitchDownPointsTheVectorUp)
{
	float d[3];
	ComputeSpotDirection(0.0, 90.0, d);
	EXPECT_NEAR(d[0], 0.f, 1e-6f); EXPECT_NEAR(d[2], 1.f, 1e-6f);
	ComputeSpotDirection(0.0, 45.0, d);
	EXPECT_NEAR(d[0], -std::sqrt(0.5f), 1e-6f); EXPECT_NEAR(d[2], std::sqrt(0.5f), 1e-6f);
	ComputeSpotDirection(0.0, -30.0, d);
	EXPECT_NEAR(d[2], -0.5f, 1e-6f);
}

// [Doruk-Aksoy] A point straight ahead takes the whole light, one beside or behind it none.
TEST(ComputeSpotFactor, AheadBesideAndBehind)
{
	float d[3];
	ComputeSpotDirection(0.0, 0.0, d);
	// [Doruk-Aksoy] The light faces east, so a point ahead of it lies east and its vector to the light points west.
	EXPECT_FLOAT_EQ(ComputeSpotFactor(-100.f, 0.f, 0.f, d, kCos10, kCos25), 1.f);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(0.f, 100.f, 0.f, d, kCos10, kCos25), 0.f);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(100.f, 0.f, 0.f, d, kCos10, kCos25), 0.f);
}

// [Doruk-Aksoy] Inside the inner cone is full, past the outer none, and between them smoothstep, half way at the midpoint of the cosines.
TEST(ComputeSpotFactor, SoftEdgeIsSmoothstep)
{
	float d[3];
	ComputeSpotDirection(0.0, 0.0, d);
	const double at = [](double deg) { return deg * 3.14159265358979323846 / 180.0; }(5.0);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(float(-std::cos(at)), float(std::sin(at)), 0.f, d, kCos10, kCos25), 1.f);
	const double past = 30.0 * 3.14159265358979323846 / 180.0;
	EXPECT_FLOAT_EQ(ComputeSpotFactor(float(-std::cos(past)), float(std::sin(past)), 0.f, d, kCos10, kCos25), 0.f);
	const float mid = (kCos10 + kCos25) / 2.f;
	const float side = std::sqrt(1.f - mid * mid);
	EXPECT_NEAR(ComputeSpotFactor(-mid, side, 0.f, d, kCos10, kCos25), 0.5f, 1e-4f);
	// [Doruk-Aksoy] A quarter of the way in, smoothstep gives 0.15625.
	const float quarter = kCos25 + (kCos10 - kCos25) * 0.25f;
	EXPECT_NEAR(ComputeSpotFactor(-quarter, std::sqrt(1.f - quarter * quarter), 0.f, d, kCos10, kCos25), 0.15625f, 1e-3f);
}

// [Doruk-Aksoy] The vector to the light need not be unit length.
TEST(ComputeSpotFactor, ScaleOfTheVectorDoesNotMatter)
{
	float d[3];
	ComputeSpotDirection(0.0, 0.0, d);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(-0.01f, 0.f, 0.f, d, kCos10, kCos25), ComputeSpotFactor(-5000.f, 0.f, 0.f, d, kCos10, kCos25));
}

// [Doruk-Aksoy] A cone whose inner angle is not inside its outer one has a hard edge at the outer, and the light's own point is lit.
TEST(ComputeSpotFactor, HardEdgeAndTheLightsOwnPoint)
{
	float d[3];
	ComputeSpotDirection(0.0, 0.0, d);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(-1.f, 0.f, 0.f, d, kCos25, kCos25), 1.f);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(-1.f, 0.6f, 0.f, d, kCos25, kCos25), 0.f);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(-1.f, 0.f, 0.f, d, kCos25, kCos10), 1.f);
	EXPECT_FLOAT_EQ(ComputeSpotFactor(0.f, 0.f, 0.f, d, kCos10, kCos25), 1.f);
}
