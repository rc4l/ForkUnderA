// [rc4l] 64-bit fixed-point scale operations for the fixed_t widening. These are the 64-bit
// analogues of computation/fixedmath.h's MulScale/DivScale, built on the wide128 primitive.
//
// Performance: the hot path stays inline here, and a 32-bit fast-path skips the 128-bit math
// whenever both operands fit in int32 -- which is every normal-range coordinate -- so normal
// maps pay nothing for the widening. The wide128 path is reached only for genuinely
// out-of-range (giant-map) operands.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_FIXED64_SCALE_COMPUTE_H
#define ZX_FIXED64_SCALE_COMPUTE_H

#include <cstdint>
#include "features/fixed64/computation/wide128_compute.h"

namespace zx
{

// [rc4l] Does v fit in a signed 32-bit range? When both operands do, their product fits in
// ~62 bits and a plain int64 intermediate is exact -- no 128-bit math needed.
inline bool Fits32(int64_t v) { return v >= INT32_MIN && v <= INT32_MAX; }

// [rc4l] (a * b) >> shift, signed, with a 128-bit intermediate when needed. shift in [0,63].
inline int64_t MulScale64(int64_t a, int64_t b, unsigned shift)
{
	if (Fits32(a) && Fits32(b))
		return (a * b) >> shift;             // fast: 62-bit product fits int64
	return ComputeMulShiftS64(a, b, shift);  // wide: giant-range operands
}

// [rc4l] (a << shift) / b, signed, truncating toward zero. shift in [0,63]; b != 0.
inline int64_t DivScale64(int64_t a, unsigned shift, int64_t b)
{
	// Fast path when the shifted numerator provably fits int64: |a| < 2^31 and shift <= 31
	// gives |a * 2^shift| < 2^62. Scale by multiply, not a left shift (shifting a negative
	// signed value is UB).
	if (Fits32(a) && shift <= 31)
		return (a * ((int64_t)1 << shift)) / b;
	return ComputeDivShiftS64(a, shift, b);  // wide: numerator exceeds 64 bits
}

// [rc4l] (a*b + c*d) >> shift -- the plane-equation DMulScale. Fast path when all operands
// fit int32 (the two products and their sum stay within int64); else the 128-bit path.
inline int64_t DMulScale64(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift)
{
	if (Fits32(a) && Fits32(b) && Fits32(c) && Fits32(d))
		return (a * b + c * d) >> shift;
	return ComputeMulAddShiftS64(a, b, c, d, shift);
}

// [rc4l] (a*b + c*d + e*f) >> shift -- TMulScale.
inline int64_t TMulScale64(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f, unsigned shift)
{
	if (Fits32(a) && Fits32(b) && Fits32(c) && Fits32(d) && Fits32(e) && Fits32(f))
		return (a * b + c * d + e * f) >> shift;
	return ComputeMulAdd3ShiftS64(a, b, c, d, e, f, shift);
}

// [rc4l] The 16.16 fixed-point multiply/divide -- the FixedMul/FixedDiv of the widened type.
inline int64_t Fixed64Mul(int64_t a, int64_t b) { return MulScale64(a, b, 16); }
inline int64_t Fixed64Div(int64_t a, int64_t b) { return DivScale64(a, 16, b); }

} // namespace zx

#endif // ZX_FIXED64_SCALE_COMPUTE_H
