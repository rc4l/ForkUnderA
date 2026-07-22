// [rc4l] Frac-interpolation of a wrapping 32-bit BAM angle -- extracted so the signed-delta
// hazard it guards is tested and documented.
//
// R_InterpolateView smooths the view between the 35Hz game tics by lerping from the previous
// tic's angle toward the current one: oldAngle + frac * (newAngle - oldAngle). The delta
// (newAngle - oldAngle) is an UNSIGNED 32-bit difference that must be read back as a SIGNED
// int32 before the multiply. A right turn makes newAngle < oldAngle, so the wrapping difference
// is a huge unsigned value (~0xFD800000 for one tic). Under 32-bit fixed_t the multiply took an
// int32 parameter, which reinterpreted those bits as the small negative delta. Once fixed_t
// widened to 64-bit the same unsigned value zero-extends to a huge POSITIVE int64, so the
// interpolated view overshoots by nearly half a circle every frame -- but only for right turns
// (negative delta); left turns produce a small positive delta and are unaffected. That is the
// "+right spins far faster than +left" bug. Casting the difference to int32 first restores the
// signed delta and makes both directions symmetric again.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_ANGLE_INTERP_COMPUTE_H
#define ZX_ANGLE_INTERP_COMPUTE_H

#include <cstdint>

#include "features/fixed64/computation/fixed64_scale_compute.h"

namespace zx
{

// [rc4l] Reinterpret a wrapping 32-bit BAM angle (or angle difference) as a SIGNED int32 before
// it is consumed as a fixed_t operand. Any BAM value at or past 180 degrees -- an absolute swing
// amplitude (a security camera's Range) or a right-turn wrapping delta -- has bit 31 set. Under
// 32-bit fixed_t that signed reinterpretation happened for free in the narrow multiply parameter;
// the widened 64-bit fixed_t instead zero-extends the value into a huge positive magnitude, which
// is the whole family of "angle read as a giant positive fixed" regressions. Casting through
// int32 restores the original signed value on both the old and new builds identically.
inline constexpr int32_t AngleAsSignedFixed(uint32_t bam) { return static_cast<int32_t>(bam); }

// [rc4l] Interpolate a wrapping BAM angle from oldAngle toward newAngle by frac (16.16). The
// difference is reinterpreted as a signed int32 before the widened multiply -- see the header
// comment for why the raw unsigned difference breaks right turns after the fixed_t widening.
inline uint32_t InterpolateAngleBAM(uint32_t oldAngle, uint32_t newAngle, int64_t frac)
{
	return oldAngle + static_cast<uint32_t>(Fixed64Mul(frac, AngleAsSignedFixed(newAngle - oldAngle)));
}

} // namespace zx

#endif // ZX_ANGLE_INTERP_COMPUTE_H
