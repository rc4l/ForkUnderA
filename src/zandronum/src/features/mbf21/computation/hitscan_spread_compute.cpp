// [rc4l] Implementation of the MBF21 hitscan-spread math. See hitscan_spread_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "hitscan_spread_compute.h"

namespace zx { namespace mbf21 {

namespace {
// Standard Doom BAM angle constants.
const int64_t ANG90            = 0x40000000;  // 90 degrees in BAM
const uint32_t ANG90U          = 0x40000000u; // 90 degrees in BAM (unsigned)
const uint32_t ANGLE_1_BAM     = 0x00B60B60u; // 1 degree in BAM
const int     ANGLETOFINESHIFT = 19;
const int     FINEANGLES       = 8192;
}

int ComputeHitscanAngleBAM(int64_t spreadBam, int rnd1, int rnd2)
{
	// DSDA takes the absolute value of spread before FixedToAngle; mirror that so a caller passing a
	// signed spread can't flip the sign of the whole distribution.
	if (spreadBam < 0)
		spreadBam = -spreadBam;
	return (int)((spreadBam * (rnd1 - rnd2)) / 255);
}

int ComputeHitscanSlopeIndex(int angleOffsetBam)
{
	const int64_t angle = angleOffsetBam;   // widen so -angle can't overflow at INT_MIN
	if (angle > ANG90)
		return 0;
	if (-angle > ANG90)
		return FINEANGLES / 2 - 1;
	// (ANG90 - angle) >> ANGLETOFINESHIFT, done fully in uint32 so the subtraction wraps by
	// definition (no signed overflow) while matching the reference for every reachable spread.
	return (int)(((uint32_t)ANG90 - (uint32_t)angleOffsetBam) >> ANGLETOFINESHIFT);
}

int ComputeDegToSlopeIndex(int64_t fixedDegrees)
{
	// Work on the magnitude; the engine re-applies the sign after the table lookup.
	int64_t a = fixedDegrees < 0 ? -fixedDegrees : fixedDegrees;
	// FixedMul(|a|, ANGLE_1): (|a| * ANGLE_1) >> 16, giving the pitch as a BAM angle.
	uint32_t ang = (uint32_t)(((uint64_t)a * ANGLE_1_BAM) >> 16);
	// Clamp just under 90 degrees so the index stays in the first quadrant of finetangent[].
	if (ang >= ANG90U)
		ang = ANG90U - 1;
	return (int)((ANG90U - ang) >> ANGLETOFINESHIFT);
}

}} // namespace zx::mbf21
