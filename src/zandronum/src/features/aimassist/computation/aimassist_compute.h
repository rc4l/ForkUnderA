// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure math of fua_debug_aimassist: where the view looks, the ticcmd turn that gets it there, and move input kept on a fixed heading.
#ifndef ZX_AIMASSIST_COMPUTE_H
#define ZX_AIMASSIST_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace aimassist {

// [Doruk-Aksoy] The engine's view pitch limits in degrees, up and down (p_user.cpp clamps to these).
constexpr double kPitchUpDeg = 32.0;
constexpr double kPitchDownDeg = 56.0;

// [Doruk-Aksoy] A view direction: yaw in degrees (0 east, counter-clockwise) and pitch in degrees, positive looking down as the engine's.
struct Aim
{
	double yawDeg;
	double pitchDeg;
};

// [Doruk-Aksoy] A tic's turn as usercmd_t holds it: yaw added and pitch taken away, each an angle shifted down 16 bits.
struct Turn
{
	short yaw;
	short pitch;
};

// [Doruk-Aksoy] Forward and side move input, side positive to the right as the engine thrusts it.
struct Move
{
	short forward;
	short side;
};

// [Doruk-Aksoy] The view from an eye to a point, its pitch held to the engine's limits.
Aim ComputeAimAt(double eyeX, double eyeY, double eyeZ, double x, double y, double z);

// [Doruk-Aksoy] Degrees as the engine's 32-bit angle (a full turn is 2^32).
uint32_t ComputeDegToAngle(double deg);

// [Doruk-Aksoy] Pitch degrees as the engine's signed fixed-point pitch.
int32_t ComputePitchDegToAngle(double deg);

// [Doruk-Aksoy] The ticcmd turn from (angle, pitch) to (wantAngle, wantPitch) in one tic, never the pitch value that centres the view instead.
Turn ComputeTicTurn(uint32_t angle, int32_t pitch, uint32_t wantAngle, int32_t wantPitch);

// [Doruk-Aksoy] Move input meant relative to headingDeg, as the same world motion relative to a view at viewDeg.
Move ComputeMoveOnHeading(int forward, int side, double headingDeg, double viewDeg);

// [Doruk-Aksoy] How far a direction is off the view, 0 to 180 degrees.
double ComputeOffDeg(double viewDeg, double dx, double dy);

// [Doruk-Aksoy] A candidate's score, lower better: its distance stretched by how far it is off the view, or -1 beyond range.
double ComputeTargetScore(double dist, double offDeg, double range);

// [Doruk-Aksoy] Candidate indices best first, those scored -1 left out.
std::vector<size_t> ComputeTargetOrder(const std::vector<double> &scores);

}} // namespace zx::aimassist

#endif
