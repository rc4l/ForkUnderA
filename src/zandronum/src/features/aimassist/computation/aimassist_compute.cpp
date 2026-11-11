// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure math of fua_debug_aimassist.
#include "features/aimassist/computation/aimassist_compute.h"

#include <algorithm>
#include <cmath>

namespace zx { namespace aimassist {

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kFullTurn = 4294967296.0;

// [Doruk-Aksoy] A value rounded to the nearest short, held to its range.
short ToShort(double v)
{
	return (short)std::max(-32768.0, std::min(32767.0, std::floor(v + 0.5)));
}
}

Aim ComputeAimAt(double eyeX, double eyeY, double eyeZ, double x, double y, double z)
{
	const double dx = x - eyeX, dy = y - eyeY, h = std::hypot(dx, dy);
	const double pitch = std::atan2(eyeZ - z, h) * 180.0 / kPi;
	return { std::atan2(dy, dx) * 180.0 / kPi, std::max(-kPitchUpDeg, std::min(kPitchDownDeg, pitch)) };
}

uint32_t ComputeDegToAngle(double deg)
{
	const double turns = deg / 360.0 - std::floor(deg / 360.0);
	return (uint32_t)(int64_t)std::floor(turns * kFullTurn + 0.5);
}

int32_t ComputePitchDegToAngle(double deg)
{
	return (int32_t)std::floor(deg / 360.0 * kFullTurn + 0.5);
}

Turn ComputeTicTurn(uint32_t angle, int32_t pitch, uint32_t wantAngle, int32_t wantPitch)
{
	const int32_t yawDelta = (int32_t)(wantAngle - angle);
	const double pitchDelta = (double)pitch - (double)wantPitch;
	Turn turn = { ToShort(yawDelta / 65536.0), ToShort(pitchDelta / 65536.0) };
	if (turn.pitch == -32768) turn.pitch = -32767;
	return turn;
}

Move ComputeMoveOnHeading(int forward, int side, double headingDeg, double viewDeg)
{
	const double d = (headingDeg - viewDeg) * kPi / 180.0, c = std::cos(d), s = std::sin(d);
	return { ToShort(forward * c + side * s), ToShort(side * c - forward * s) };
}

double ComputeOffDeg(double viewDeg, double dx, double dy)
{
	const double diff = std::fmod(std::atan2(dy, dx) * 180.0 / kPi - viewDeg, 360.0);
	const double wrapped = diff < -180.0 ? diff + 360.0 : diff > 180.0 ? diff - 360.0 : diff;
	return std::fabs(wrapped);
}

double ComputeTargetScore(double dist, double offDeg, double range)
{
	if (dist > range) return -1.0;
	return dist * (1.0 + offDeg / 45.0);
}

std::vector<size_t> ComputeTargetOrder(const std::vector<double> &scores)
{
	std::vector<size_t> order;
	for (size_t i = 0; i < scores.size(); i++)
		if (scores[i] >= 0.0) order.push_back(i);
	std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return scores[a] < scores[b]; });
	return order;
}

}} // namespace zx::aimassist
