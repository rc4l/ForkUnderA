// [rc4l] Distance-based physics reductions that must keep P_AproxDistance's result at full
// 64-bit width until the final reduction.
//
// Both call sites were `int dist = P_AproxDistance(...)` before the fixed_t widening. That was
// harmless when P_AproxDistance returned a 32-bit fixed_t (int == fixed_t), but once it returns
// 64-bit the int truncates any span beyond ~32k units in fixed -- reachable on a normal
// full-size map (two things ~46k units apart is a fixed value > INT32_MAX) -- flipping the sign
// of the distance and corrupting the push/seek. Keeping dist as fixed_t through the reduction
// fixes it. Extracted so the reductions are unit-tested with far-distance inputs.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_DIST_COMPUTE_H
#define ZX_DIST_COMPUTE_H

#include <cstdint>

namespace zx
{

// [rc4l] Boom point-pusher (sc_push) speed: the push magnitude falls off with half the distance
// in map units, then scales into a velocity. `dist` is a full-width fixed_t distance; magnitude
// and pushFactor are small ints. The final shift is done through unsigned so it stays defined for
// the "outside the radius" case where the intermediate goes negative (the engine used a signed
// shift there). Bit-for-bit identical to the old `(mag - ((dist>>FRACBITS)>>1)) << k` for in-range
// results -- it just no longer truncates dist first.
inline int ComputePusherSpeed(int64_t dist, int magnitude, int pushFactor, int fracBits)
{
	const int32_t inner = magnitude - static_cast<int32_t>((dist >> fracBits) >> 1);
	const int shift = fracBits - pushFactor - 1;
	return static_cast<int32_t>(static_cast<uint32_t>(inner) << shift);
}

// [rc4l] Homing/seeker missile vertical velocity. dist/speed is the number of tics to reach the
// target's XY position; velz closes the Z gap (zdiff) over that many tics. dist, speed and zdiff
// stay full-width fixed_t. Precondition: speed != 0 (the caller checks and bails otherwise).
inline int64_t ComputeSeekerVelZ(int64_t dist, int64_t speed, int64_t zdiff)
{
	int64_t tics = dist / speed;
	if (tics < 1)
		tics = 1;
	return zdiff / tics;
}

} // namespace zx

#endif // ZX_DIST_COMPUTE_H
