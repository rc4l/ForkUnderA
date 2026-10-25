// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Implementation of the pure OpenAL panning model; no engine or OpenAL dependencies.
#include "features/openal-sound/computation/oal_pan_compute.h"
#include <algorithm>
#include <cmath>

namespace
{
// [Doruk-Aksoy] OpenAL Soft's StereoConfig decoder row (alc/panning.cpp) on W, Y and X, N3D scaled.
constexpr float kDecoderW = 5.00000000e-1f;
constexpr float kDecoderY = 2.88675135e-1f;
constexpr float kDecoderX = 5.52305643e-2f;
constexpr float kSqrt3 = 1.73205081f;
constexpr float kPi = 3.14159265f;
// [Doruk-Aksoy] At or below this distance OpenAL Soft plays a source as on the listener (FLT_EPSILON).
constexpr float kOnListener = 1.1920929e-7f;

float Length(ZxPanVector v)
{
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// [Doruk-Aksoy] OpenAL Soft's ScaleAzimuthFront3_2: a front direction's azimuth times 1.5, clamped to the speaker from 60 degrees off centre.
ZxPanVector ScaleAzimuthFront(ZxPanVector pos)
{
	if (pos.z >= 0.f)
		return pos;
	const float len2d = std::sqrt(pos.x * pos.x + pos.z * pos.z);
	float z = -pos.z / len2d;
	if (z > 0.5f)
	{
		float x = std::copysign(std::sqrt((1.f - z) * 0.5f), pos.x);
		z = std::sqrt((1.f + z) * 0.5f);
		x = x * 3.f - x * x * x * 4.f;
		z = z * z * z * 4.f - z * 3.f;
		pos.x = x * len2d;
		pos.z = -z * len2d;
	}
	else
	{
		pos.x = std::copysign(len2d, pos.x);
		pos.z = 0.f;
	}
	return pos;
}
}

ZxPanVector ComputeListenerFrameOffset(float dx, float dy, float dz, float listenerAngle)
{
	// [Doruk-Aksoy] UpdateListener faces OpenAL along (cos, 0, -sin) with the engine's Z negated, so in engine space ahead is (cos, 0, sin) and right is (sin, 0, -cos).
	const float c = std::cos(listenerAngle);
	const float s = std::sin(listenerAngle);
	return ZxPanVector{ dx * s - dz * c, dy, -(dx * c + dz * s) };
}

float ComputeOalSpread(float distance, float radius)
{
	// [Doruk-Aksoy] A distance is never negative, so radius > 0 only spells out what OpenAL Soft's own test implies.
	if (radius > 0.f && radius > distance)
		return kPi * 2.f - distance / radius * kPi;
	if (distance > 0.f)
		return std::asin(radius / distance) * 2.f;
	return 0.f;
}

ZxStereoGains ComputeOalMonoStereoPan(ZxPanVector offset, float spread)
{
	ZxPanVector dir{ 0.f, 0.f, -1.f };
	const float distance = Length(offset);
	if (distance > kOnListener)
		dir = ScaleAzimuthFront(ZxPanVector{ offset.x / distance, offset.y / distance, offset.z / distance });

	// [Doruk-Aksoy] CalcAmbiCoeffs' spread lifts the whole source by up to 3 dB and narrows its directional terms.
	float omni = 1.f;
	float directional = 1.f;
	if (spread > 0.f)
	{
		omni = std::sqrt(1.f + spread / (kPi * 2.f));
		directional = omni * 0.5f * (std::cos(spread * 0.5f) + 1.f);
	}
	const float centre = kDecoderW * omni + kDecoderX * directional * kSqrt3 * -dir.z;
	const float side = kDecoderY * directional * kSqrt3 * -dir.x;
	return ZxStereoGains{ centre + side, centre - side };
}

float ComputeMonoPanGain(bool basicStereo, int bufferChannels, ZxPanVector offset, float radius)
{
	if (!basicStereo || bufferChannels != 1)
		return 1.f;
	const ZxStereoGains pan = ComputeOalMonoStereoPan(offset, ComputeOalSpread(Length(offset), radius));
	return 1.f / std::sqrt(pan.left * pan.left + pan.right * pan.right);
}

bool ComputeBasicStereoOutput(bool hasOutputMode, int outputMode)
{
	return hasOutputMode && outputMode == ZX_ALC_STEREO_BASIC_SOFT;
}

float ComputeSourceMaxGain(bool aboveOneAccepted, float maxVolume)
{
	if (!aboveOneAccepted)
		return 1.f;
	// [Doruk-Aksoy] A point source straight behind is the quietest OpenAL Soft pans, so it needs the largest lift.
	return maxVolume * ComputeMonoPanGain(true, 1, ZxPanVector{ 0.f, 0.f, 1.f }, 0.f);
}

float ComputeSourceRadius(bool hasRadiusExt, bool area, bool manualRolloff, float areaRadius, float rolloffAtArea)
{
	if (!hasRadiusExt || !area)
		return 0.f;
	if (manualRolloff)
		return 1.f / std::max(rolloffAtArea, 0.00001f);
	return areaRadius;
}
