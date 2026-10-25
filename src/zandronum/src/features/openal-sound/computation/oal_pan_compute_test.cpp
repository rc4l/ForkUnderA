// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Tests for the OpenAL panning model, against levels measured through OpenAL Soft 1.25.1's own DLL on a loopback device.
#include "gtest/gtest.h"
#include "features/openal-sound/computation/oal_pan_compute.h"
#include <cmath>

namespace
{
constexpr float kPi = 3.14159265f;

float Db(float gain)
{
	return 20.f * std::log10(gain);
}

// [Doruk-Aksoy] A source 10 units away, az degrees to the left and el degrees up, in OpenAL's listener frame.
ZxPanVector At(float az, float el)
{
	const float a = az * kPi / 180.f;
	const float e = el * kPi / 180.f;
	return ZxPanVector{ -10.f * std::sin(a) * std::cos(e), 10.f * std::sin(e), -10.f * std::cos(a) * std::cos(e) };
}

struct Measured
{
	float az, el, radius, leftDb, rightDb;
};

// [Doruk-Aksoy] dB per speaker from OpenAL Soft 1.25.1 (stereo float loopback, no HRTF), as ZandroX sets up a 3D source.
const Measured kMeasured[] = {
	{ 0.f, 0.f, 0.f, -4.50f, -4.50f },
	{ 20.f, 0.f, 0.f, -1.59f, -9.56f },
	{ 30.f, 0.f, 0.f, -0.71f, -13.39f },
	{ 45.f, 0.f, 0.f, -0.01f, -22.54f },
	{ 135.f, 0.f, 0.f, -2.09f, -22.07f },
	{ 180.f, 0.f, 0.f, -7.87f, -7.87f },
	{ 30.f, 45.f, 0.f, -1.96f, -10.52f },
	{ 0.f, 90.f, 0.f, -6.02f, -6.02f },
	{ 45.f, 0.f, 5.f, 0.36f, -19.06f },
	{ 90.f, 0.f, 10.f, -0.74f, -10.28f },
	{ 180.f, 0.f, 20.f, -3.84f, -3.84f },
};

// [Doruk-Aksoy] Facing east, north is to the left and east ahead.
TEST(ComputeListenerFrameOffset, FacingEast)
{
	const ZxPanVector north = ComputeListenerFrameOffset(0.f, 0.f, 1.f, 0.f);
	EXPECT_FLOAT_EQ(north.x, -1.f);
	EXPECT_FLOAT_EQ(north.z, 0.f);
	const ZxPanVector east = ComputeListenerFrameOffset(1.f, 0.f, 0.f, 0.f);
	EXPECT_FLOAT_EQ(east.x, 0.f);
	EXPECT_FLOAT_EQ(east.z, -1.f);
	EXPECT_FLOAT_EQ(ComputeListenerFrameOffset(0.f, 3.f, 0.f, 0.f).y, 3.f);
}

// [Doruk-Aksoy] Facing north, north is ahead and east to the right.
TEST(ComputeListenerFrameOffset, FacingNorth)
{
	const ZxPanVector north = ComputeListenerFrameOffset(0.f, 0.f, 1.f, kPi / 2.f);
	EXPECT_NEAR(north.x, 0.f, 1e-6f);
	EXPECT_NEAR(north.z, -1.f, 1e-6f);
	const ZxPanVector east = ComputeListenerFrameOffset(1.f, 0.f, 0.f, kPi / 2.f);
	EXPECT_NEAR(east.x, 1.f, 1e-6f);
	EXPECT_NEAR(east.z, 0.f, 1e-6f);
}

// [Doruk-Aksoy] A point source has no spread, one on the listener inside its radius has the full circle.
TEST(ComputeOalSpread, EveryCase)
{
	EXPECT_FLOAT_EQ(ComputeOalSpread(10.f, 0.f), 0.f);
	EXPECT_FLOAT_EQ(ComputeOalSpread(0.f, 0.f), 0.f);
	EXPECT_FLOAT_EQ(ComputeOalSpread(0.f, 5.f), 2.f * kPi);
	EXPECT_FLOAT_EQ(ComputeOalSpread(5.f, 10.f), 1.5f * kPi);
	EXPECT_FLOAT_EQ(ComputeOalSpread(10.f, 5.f), 2.f * std::asin(0.5f));
}

// [Doruk-Aksoy] The model gives what OpenAL Soft itself measured, to the measurement's rounding.
TEST(ComputeOalMonoStereoPan, MatchesOpenAlSoft)
{
	for (const Measured &m : kMeasured)
	{
		const ZxPanVector offset = At(m.az, m.el);
		const ZxStereoGains pan = ComputeOalMonoStereoPan(offset, ComputeOalSpread(10.f, m.radius));
		EXPECT_NEAR(Db(pan.left), m.leftDb, 0.02f) << "az " << m.az << " el " << m.el << " radius " << m.radius;
		EXPECT_NEAR(Db(pan.right), m.rightDb, 0.02f) << "az " << m.az << " el " << m.el << " radius " << m.radius;
	}
}

// [Doruk-Aksoy] From 60 degrees off centre a front source plays on one speaker alone.
TEST(ComputeOalMonoStereoPan, FrontClampsToTheSpeaker)
{
	const ZxStereoGains pan = ComputeOalMonoStereoPan(At(75.f, 0.f), 0.f);
	EXPECT_NEAR(pan.left, 1.f, 1e-5f);
	EXPECT_NEAR(pan.right, 0.f, 1e-5f);
	const ZxStereoGains side = ComputeOalMonoStereoPan(ZxPanVector{ -1.f, 0.f, 0.f }, 0.f);
	EXPECT_FLOAT_EQ(side.left, 1.f);
	EXPECT_NEAR(side.right, 0.f, 1e-6f);
}

// [Doruk-Aksoy] A source on the listener plays front centre, at -4.5 dB alone and -3 dB with a radius (full spread).
TEST(ComputeOalMonoStereoPan, OnTheListener)
{
	const ZxStereoGains point = ComputeOalMonoStereoPan(ZxPanVector{ 0.f, 0.f, 0.f }, 0.f);
	EXPECT_NEAR(Db(point.left), -4.50f, 0.01f);
	EXPECT_FLOAT_EQ(point.left, point.right);
	const ZxStereoGains wide = ComputeOalMonoStereoPan(ZxPanVector{ 0.f, 0.f, 0.f }, ComputeOalSpread(0.f, 1.f));
	EXPECT_NEAR(Db(wide.left), -3.01f, 0.01f);
	EXPECT_FLOAT_EQ(wide.left, wide.right);
}

// [Doruk-Aksoy] Only a mono buffer on basic stereo is lifted.
TEST(ComputeMonoPanGain, OnlyMonoOnBasicStereo)
{
	EXPECT_FLOAT_EQ(ComputeMonoPanGain(false, 1, At(0.f, 0.f), 0.f), 1.f);
	EXPECT_FLOAT_EQ(ComputeMonoPanGain(true, 2, At(0.f, 0.f), 0.f), 1.f);
	EXPECT_NEAR(Db(ComputeMonoPanGain(true, 1, At(0.f, 0.f), 0.f)), 1.49f, 0.01f);
}

// [Doruk-Aksoy] Centred mono lands on FMOD Ex's -3 dB per speaker, the level Zandronum plays it at.
TEST(ComputeMonoPanGain, CentreMatchesEqualPower)
{
	const float gain = ComputeMonoPanGain(true, 1, ZxPanVector{ 0.f, 0.f, 0.f }, 0.f);
	const ZxStereoGains pan = ComputeOalMonoStereoPan(ZxPanVector{ 0.f, 0.f, 0.f }, 0.f);
	EXPECT_NEAR(Db(pan.left * gain), -3.01f, 0.01f);
	EXPECT_NEAR(Db(pan.right * gain), -3.01f, 0.01f);
}

// [Doruk-Aksoy] Every direction, height and radius comes out at full total power, never past the source's max gain.
TEST(ComputeMonoPanGain, ConstantPowerEverywhere)
{
	const float ceiling = ComputeSourceMaxGain(true, 1.f);
	for (float radius : { 0.f, 5.f, 10.f, 20.f, 40.f })
	{
		for (float el = -90.f; el <= 90.f; el += 15.f)
		{
			for (float az = 0.f; az < 360.f; az += 15.f)
			{
				const ZxPanVector offset = At(az, el);
				const float gain = ComputeMonoPanGain(true, 1, offset, radius);
				// [Doruk-Aksoy] The spread from the offset's own length, since at radius == distance asin is too steep for a rounded 10.
				const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
				const ZxStereoGains pan = ComputeOalMonoStereoPan(offset, ComputeOalSpread(distance, radius));
				const float power = (pan.left * pan.left + pan.right * pan.right) * gain * gain;
				EXPECT_NEAR(power, 1.f, 1e-4f) << "az " << az << " el " << el << " radius " << radius;
				EXPECT_LE(gain, ceiling + 1e-5f) << "az " << az << " el " << el << " radius " << radius;
			}
		}
	}
}

// [Doruk-Aksoy] Only basic stereo output qualifies, and only when the device can say so.
TEST(ComputeBasicStereoOutput, EveryCase)
{
	EXPECT_TRUE(ComputeBasicStereoOutput(true, ZX_ALC_STEREO_BASIC_SOFT));
	EXPECT_FALSE(ComputeBasicStereoOutput(false, ZX_ALC_STEREO_BASIC_SOFT));
	EXPECT_FALSE(ComputeBasicStereoOutput(true, 0x19B2));
}

// [Doruk-Aksoy] The ceiling covers the loudest volume behind the listener, or stays at 1 where more is refused.
TEST(ComputeSourceMaxGain, EveryCase)
{
	EXPECT_FLOAT_EQ(ComputeSourceMaxGain(false, 2.f), 1.f);
	EXPECT_NEAR(ComputeSourceMaxGain(true, 2.f), 2.f * 1.7488f, 1e-3f);
}

// [Doruk-Aksoy] The radius is the area's own, its manual-rolloff stand-in, or none.
TEST(ComputeSourceRadius, EveryCase)
{
	EXPECT_FLOAT_EQ(ComputeSourceRadius(false, true, false, 128.f, 0.5f), 0.f);
	EXPECT_FLOAT_EQ(ComputeSourceRadius(true, false, false, 128.f, 0.5f), 0.f);
	EXPECT_FLOAT_EQ(ComputeSourceRadius(true, true, false, 128.f, 0.5f), 128.f);
	EXPECT_FLOAT_EQ(ComputeSourceRadius(true, true, true, 128.f, 0.5f), 2.f);
	EXPECT_FLOAT_EQ(ComputeSourceRadius(true, true, true, 128.f, 0.f), 100000.f);
}
}
