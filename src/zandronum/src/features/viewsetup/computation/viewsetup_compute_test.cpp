// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/viewsetup/computation/viewsetup_compute.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <math.h>

using namespace zx::viewsetup;

TEST(ViewSetup, PitchStretchesWithThePixelRatio)
{
	EXPECT_FLOAT_EQ(ComputeStretchedPitch(0, 1.2f), 0.f);
	// [rc4l] 30 degrees down at square pixels stays 30, and steepens once each pixel is 1.2 tall.
	const unsigned down30 = 0x15555555u;
	EXPECT_NEAR(ComputeStretchedPitch(down30, 1.f), 30.f, 1e-4);
	EXPECT_NEAR(ComputeStretchedPitch(down30, 1.2f), 34.7150f, 1e-3);
	EXPECT_NEAR(ComputeStretchedPitch(0u - down30, 1.f), -30.f, 1e-4);   // [rc4l] up wraps past half a turn to negative
}

TEST(ViewSetup, PitchIsClampedToStraightUpAndDown)
{
	EXPECT_FLOAT_EQ(ComputeStretchedPitch(0x50000000u, 1.f), 90.f);   // [rc4l] past straight down
	EXPECT_FLOAT_EQ(ComputeStretchedPitch(0xB0000000u, 1.f), -90.f);   // [rc4l] past straight up
}

namespace {

const double kRad = 3.14159265358979323846 / 180.0;

// [rc4l] A lens and GL's aspect multiplier for it: 4:3, 5:4 (taller, with its own fov ratio), 16:9 and 21:9.
struct Lens { float ratio, fovRatio; int multiplier; };
const Lens kLenses[] = { { 4.f / 3.f, 1.333333f, 48 }, { 1.25f, 1.25f, 45 }, { 16.f / 9.f, 1.333333f, 36 }, { 7.f / 3.f, 1.333333f, 27 } };

FrustumIn Through(const Lens &l, float pitch, float roll = 0.f, float fov = 90.f)
{
	const FrustumIn in = { pitch, roll, fov, l.ratio, l.fovRatio, l.multiplier };
	return in;
}

double Degrees(unsigned bams) { return bams / 2147483648.0 * 180.0; }

// [rc4l] The widest heading of a 9 by 9 grid of rays across the screen, rolled and then pitched by plain rotation, or -1 when one points behind the eye.
double SampledHeading(const FrustumIn &in)
{
	const double ty = tan(in.fovDeg * 0.5 * kRad) / in.fovRatio, tx = ty * in.ratio;
	double widest = 0.0;
	for (int i = 0; i <= 8; i++)
		for (int j = 0; j <= 8; j++)
		{
			const double sx = (i / 4.0 - 1.0) * tx, sy = (j / 4.0 - 1.0) * ty;
			const double x = sx * cos(in.rollDeg * kRad) + sy * sin(in.rollDeg * kRad);
			const double y = -sx * sin(in.rollDeg * kRad) + sy * cos(in.rollDeg * kRad);
			const double ahead = cos(in.pitchDeg * kRad) + y * sin(in.pitchDeg * kRad);
			if (ahead <= 0.0) return -1.0;
			widest = std::max(widest, atan2(fabs(x), ahead) / kRad);
		}
	return widest;
}

} // namespace

TEST(ViewSetup, ALevelFrustumIsGLsEstimate)
{
	const unsigned level = ComputeFrustumAngle(Through(kLenses[0], 0.f));   // [rc4l] 4:3 at 90 degrees: 47 degrees either side, never narrower
	EXPECT_GE(level, 0x216C16C1u);
	EXPECT_NEAR(Degrees(level), 47.0, 1e-4);
	EXPECT_GT(ComputeFrustumAngle(Through(kLenses[0], -20.f)), level);
	EXPECT_NEAR(Degrees(ComputeFrustumAngle(Through(kLenses[2], 0.f))), 62.0, 1e-6);   // [rc4l] 16:9's estimate, wider than its corners need
}

TEST(ViewSetup, TheFrustumOpensFullyWhenTheViewSeesAllRound)
{
	EXPECT_EQ(ComputeFrustumAngle(Through(kLenses[0], 47.f)), 0xffffffffu);              // [rc4l] GL's: pitched past 46 degrees
	EXPECT_EQ(ComputeFrustumAngle(Through(kLenses[0], 10.f, 0.f, 400.f)), 0xffffffffu);  // [rc4l] a lens of half a turn or more
	EXPECT_EQ(ComputeFrustumAngle(Through(kLenses[0], 40.f, 0.f, 150.f)), 0xffffffffu);  // [rc4l] pitched until the bottom corners point behind the eye
	const Lens ultrawide = { 32.f / 9.f, 1.333333f, 18 };
	EXPECT_EQ(ComputeFrustumAngle(Through(ultrawide, 0.f, 0.f, 150.f)), 0xffffffffu);    // [rc4l] an estimate past half a turn
}

// [rc4l] Queue #58: a 4:3 lens pitched 44 degrees (38.85 before the pixel stretch) sees 78.8 degrees out at its bottom corners, past GL's estimate of 70.2, which cut the corner out of the walk.
TEST(ViewSetup, APitchedFrustumReachesTheScreensCorners)
{
	const double estimate = 2.0 + (45.0 + 44.0 / 1.9);
	const double corner = atan2(1.0, cos(44.0 * kRad) - 0.75 * sin(44.0 * kRad)) / kRad;
	const double angle = Degrees(ComputeFrustumAngle(Through(kLenses[0], 44.f)));
	EXPECT_NEAR(angle, corner + 2.0, 1e-4);
	EXPECT_GT(corner, estimate + 8.0);
}

// [rc4l] Whatever the lens, pitch and roll, the frustum reaches every ray the screen shows, and opens all round when one points behind the eye.
TEST(ViewSetup, TheFrustumReachesEveryRayOnTheScreen)
{
	for (const Lens &l : kLenses)
		for (float fov : { 75.f, 90.f, 110.f })
			for (float pitch = -46.f; pitch <= 46.f; pitch += 2.f)
				for (float roll : { 0.f, 20.f, -45.f, 90.f })
				{
					const FrustumIn in = Through(l, pitch, roll, fov);
					const unsigned angle = ComputeFrustumAngle(in);
					const double sampled = SampledHeading(in);
					if (sampled < 0.0) EXPECT_EQ(angle, 0xffffffffu) << l.ratio << " fov " << fov << " pitch " << pitch << " roll " << roll;
					else if (angle != 0xffffffffu) EXPECT_GE(Degrees(angle), sampled + 1.99) << l.ratio << " fov " << fov << " pitch " << pitch << " roll " << roll;
				}
}

TEST(ViewSetup, TheSkyScrollsWithTheClockAndWraps)
{
	EXPECT_FLOAT_EQ(ComputeSkyScroll(0, 0.5f), 0.f);
	EXPECT_FLOAT_EQ(ComputeSkyScroll(512, 1.f), 512.f * 90.f / 256.f);
	EXPECT_FLOAT_EQ(ComputeSkyScroll(1536, 1.f), 512.f * 90.f / 256.f);   // [rc4l] a wrapped clock scrolls as far
}

TEST(ViewSetup, TheViewAngleGivesTheYawAndFacing)
{
	float yaw, x, y;
	ComputeViewAngleVector(0, yaw, x, y);
	EXPECT_FLOAT_EQ(yaw, 270.f); EXPECT_FLOAT_EQ(x, 1.f); EXPECT_NEAR(y, 0.f, 1e-6f);
	ComputeViewAngleVector(0x40000000u, yaw, x, y);   // [rc4l] a quarter turn: north
	EXPECT_FLOAT_EQ(yaw, 180.f); EXPECT_NEAR(x, 0.f, 1e-6f); EXPECT_FLOAT_EQ(y, 1.f);
}

TEST(ViewSetup, TheViewAreaAgainstAHeightTransfer)
{
	EXPECT_EQ(ComputeViewArea(false, 0, 0, 0, false), kAreaDefault);
	EXPECT_EQ(ComputeViewArea(true, 10, 10, 100, false), kAreaBelow);
	EXPECT_EQ(ComputeViewArea(true, 50, 10, 100, false), kAreaNormal);
	EXPECT_EQ(ComputeViewArea(true, 150, 10, 100, false), kAreaAbove);
	EXPECT_EQ(ComputeViewArea(true, 150, 10, 100, true), kAreaNormal);   // [rc4l] a floor-only transfer has no above
}

TEST(ViewSetup, TheEyeStaysFourUnitsInside)
{
	const long long u = 1 << 16;
	EXPECT_EQ(ComputeClampedViewZ(50 * u, 0, 100 * u), 50 * u);
	EXPECT_EQ(ComputeClampedViewZ(99 * u, 0, 100 * u), 96 * u);
	EXPECT_EQ(ComputeClampedViewZ(1 * u, 0, 100 * u), 4 * u);
	EXPECT_EQ(ComputeClampedViewZ(1 * u, 0, 2 * u), 4 * u);   // [rc4l] crossed: the floor wins
}

TEST(ViewSetup, TheFovRatioIsFourThirdsUnlessTheScreenIsTall)
{
	EXPECT_FLOAT_EQ(ComputeFovRatio(16.f / 9.f, false), 1.333333f);
	EXPECT_FLOAT_EQ(ComputeFovRatio(1.25f, true), 1.25f);
}

TEST(ComputeMaxViewPitch, TheSoftwareLimitsOnlyWhenForced)
{
	EXPECT_EQ(90, zx::viewsetup::ComputeMaxViewPitch(true, false, true, 90));
	EXPECT_EQ(56, zx::viewsetup::ComputeMaxViewPitch(true, true, true, 90));
	EXPECT_EQ(32, zx::viewsetup::ComputeMaxViewPitch(false, true, true, 90));
	EXPECT_EQ(56, zx::viewsetup::ComputeMaxViewPitch(false, true, false, 90));
}

TEST(ComputeFixedColormap, TheInverseMapThenThePlayersThenALightAmplifiers)
{
	using namespace zx::viewsetup;
	FixedColormapIn in = { true, false, kNoFixedColormap, -1, kNoLightPower };
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
	in.lightPower = kTorchPower;
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
	in.fixedLightLevel = 3;
	EXPECT_EQ(3 + kCmTorch, ComputeFixedColormap(in));
	in.lightPower = kLightAmpPower;
	EXPECT_EQ(kCmLite, ComputeFixedColormap(in));
	in.lightPower = kNoLightPower;
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
	in.fixedColormap = 2;
	EXPECT_EQ(kCmFirstSpecial + 2, ComputeFixedColormap(in));
	in.inverse = true;
	EXPECT_EQ(kCmFirstSpecial + kInverseColormap, ComputeFixedColormap(in));
	in.hasPlayer = false;
	EXPECT_EQ(kCmDefault, ComputeFixedColormap(in));
}

TEST(ComputeSeesOwnBody, TheChaseCameraOrADeadPlayersDeathCamera)
{
	using zx::viewsetup::ComputeSeesOwnBody;
	EXPECT_TRUE(ComputeSeesOwnBody(true, false, true, false, true, false, false, true));
	EXPECT_TRUE(ComputeSeesOwnBody(true, false, true, false, false, true, true, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, false, true, false, false, true, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(false, false, true, false, true, false, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, true, true, false, true, false, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, false, true, true, true, false, false, true));
	EXPECT_TRUE(ComputeSeesOwnBody(true, false, false, true, true, false, false, true));
	EXPECT_FALSE(ComputeSeesOwnBody(true, false, true, false, true, false, false, false));
}

// [rc4l] Invulnerability's inverse map: grey 0 is the start colour, white the end, as main.fp's case 1 ramps it.
TEST(ComputeFixedColormapShade, ASpecialColormapIsItsRampFromStartToEnd)
{
	const float start[3] = { 1.f, 0.5f, 0.25f }, end[3] = { 0.f, 1.f, 0.25f };
	const FixedColormapShade s = ComputeFixedColormapShade(kCmFirstSpecial + 1, 2, start, end, false);
	EXPECT_EQ(kShadeRamp, s.mode);
	EXPECT_FLOAT_EQ(1.f, s.start[0]); EXPECT_FLOAT_EQ(0.5f, s.start[1]); EXPECT_FLOAT_EQ(0.25f, s.start[2]);
	EXPECT_FLOAT_EQ(-1.f, s.range[0]); EXPECT_FLOAT_EQ(0.5f, s.range[1]); EXPECT_FLOAT_EQ(0.f, s.range[2]);
	// [rc4l] One past the specials, or none at all, is no colormap.
	EXPECT_EQ(kShadeNone, ComputeFixedColormapShade(kCmFirstSpecial + 2, 2, start, end, false).mode);
	EXPECT_EQ(kShadeNone, ComputeFixedColormapShade(kCmDefault, 2, start, end, false).mode);
}

// [rc4l] Light amplification is white, or night-vision green with r_enhanced_nightvision; a fog layer is the sprite's mode 3.
TEST(ComputeFixedColormapShade, LightAmplificationTintsAndAFogLayerIsItsOwnMode)
{
	const float z[3] = { 0.f, 0.f, 0.f };
	FixedColormapShade s = ComputeFixedColormapShade(kCmLite, 1, z, z, false);
	EXPECT_EQ(kShadeTint, s.mode);
	EXPECT_EQ(1.f, s.start[0]); EXPECT_EQ(1.f, s.start[1]); EXPECT_EQ(1.f, s.start[2]);
	s = ComputeFixedColormapShade(kCmLite, 1, z, z, true);
	EXPECT_EQ(0.375f, s.start[0]); EXPECT_EQ(1.f, s.start[1]); EXPECT_EQ(0.375f, s.start[2]);
	EXPECT_EQ(kShadeFogLayer, ComputeFixedColormapShade(kCmFogLayer, 1, z, z, false).mode);
}

// [rc4l] A torch flickers from 0.9 at light 0 down to 0.8 at 7, its blue a quarter lower in night vision.
TEST(ComputeFixedColormapShade, ATorchFlickersByItsLightLevel)
{
	const float z[3] = { 0.f, 0.f, 0.f };
	FixedColormapShade s = ComputeFixedColormapShade(kCmTorch, 1, z, z, false);
	EXPECT_EQ(kShadeTint, s.mode);
	EXPECT_FLOAT_EQ(0.9f, s.start[0]); EXPECT_FLOAT_EQ(0.9f, s.start[2]);
	s = ComputeFixedColormapShade(kCmTorch + 7, 1, z, z, true);
	EXPECT_FLOAT_EQ(0.8f, s.start[0]); EXPECT_FLOAT_EQ(0.8f, s.start[1]); EXPECT_FLOAT_EQ(0.6f, s.start[2]);
}
