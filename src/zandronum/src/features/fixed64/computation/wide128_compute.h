// [rc4l] 128-bit intermediate primitives for the 64-bit fixed_t migration (Phase 0).
//
// 64-bit fixed-point multiply-shift needs a 128-bit product and divide-shift needs a
// 128-bit numerator. clang/gcc get this from __int128; MSVC has no __int128, so this
// supplies a portable software path (used on ARM64 MSVC, which also lacks _div128). The
// software routines are exposed separately from the public API so they can be unit-tested
// directly on any compiler -- the risky code is covered even where it does not run.
//
// Standalone (only <cstdint>) so it is unit-testable without the engine.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_WIDE128_COMPUTE_H
#define ZX_WIDE128_COMPUTE_H

#include <cstdint>

namespace zx
{

// [rc4l] Public: signed (a*b) >> shift with a 128-bit intermediate. shift in [0,63].
// Result is assumed to fit int64_t (true for fixed-point coordinates); on overflow it
// carries the low 64 bits, matching a native __int128 truncation.
int64_t ComputeMulShiftS64(int64_t a, int64_t b, unsigned shift);

// [rc4l] Public: signed (a << shift) / b, truncating toward zero (C semantics), with a
// 128-bit numerator. shift in [0,63]. Caller guarantees b != 0.
int64_t ComputeDivShiftS64(int64_t a, unsigned shift, int64_t b);

// --- Software primitives (always compiled; exposed for direct testing) ------------------

// [rc4l] Full unsigned 64x64 -> 128 product. Returns the low 64 bits; *hi gets the high 64.
uint64_t ComputeUMul128Soft(uint64_t a, uint64_t b, uint64_t *hi);

// [rc4l] Low 64 bits of the unsigned 128-bit value (hi:lo) divided by d. Restoring bitwise
// long division, correct for the full 128-bit dividend. Caller guarantees d != 0.
uint64_t ComputeUDiv128Soft(uint64_t hi, uint64_t lo, uint64_t d);

// [rc4l] Software implementations of the two public functions, used as the MSVC path and
// tested directly against the native reference.
int64_t ComputeMulShiftS64Soft(int64_t a, int64_t b, unsigned shift);
int64_t ComputeDivShiftS64Soft(int64_t a, unsigned shift, int64_t b);

// [rc4l] Public: (a*b + c*d) >> shift and (a*b + c*d + e*f) >> shift with a 128-bit
// intermediate, for the plane-equation DMulScale/TMulScale. shift in [0,63].
int64_t ComputeMulAddShiftS64(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift);
int64_t ComputeMulAdd3ShiftS64(int64_t a, int64_t b, int64_t c, int64_t d,
	int64_t e, int64_t f, unsigned shift);

// [rc4l] Software versions, exposed for direct testing (the MSVC path).
int64_t ComputeMulAddShiftS64Soft(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift);
int64_t ComputeMulAdd3ShiftS64Soft(int64_t a, int64_t b, int64_t c, int64_t d,
	int64_t e, int64_t f, unsigned shift);

// [rc4l] Public: (a*b) / c with a 128-bit product, truncating toward zero -- the generic
// Scale(a,b,c). Caller guarantees c != 0.
int64_t ComputeMulDivS64(int64_t a, int64_t b, int64_t c);
int64_t ComputeMulDivS64Soft(int64_t a, int64_t b, int64_t c);

} // namespace zx

#endif // ZX_WIDE128_COMPUTE_H
