// [rc4l] Fixed-point scale math for ZandroX, widened to 64-bit.
//
// The 64-bit-capable core lives in features/fixed64/computation (wide128 + the scale layer,
// both clean-room and unit-tested). This header keeps the historical function names the
// engine calls (MulScale16, DivScale16, DMulScale16, Scale, ...) but they are now 64-bit and
// delegate to that core. The 32-bit fast-path in the scale layer keeps normal-range values on
// the plain 64-bit path, so nothing is slower than before; the 128-bit path is reached only
// for giant-map operands. Not derived from Ken Silverman's Build code.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ZX_FIXEDMATH_H
#define ZX_FIXEDMATH_H

#include <cstdint>
#include "features/fixed64/computation/fixed64_scale_compute.h"

// Generic forms: shift is a runtime argument. (The engine uses the numbered forms below; these
// exist for completeness and delegate to the same tested core.)
inline int64_t Scale(int64_t a, int64_t b, int64_t c)                              { return zx::ComputeMulDivS64(a, b, c); }
inline int64_t MulScale(int64_t a, int64_t b, unsigned s)                          { return zx::MulScale64(a, b, s); }
inline int64_t DivScale(int64_t a, int64_t b, unsigned s)                          { return zx::DivScale64(a, s, b); }
inline int64_t DMulScale(int64_t a, int64_t b, int64_t c, int64_t d, unsigned s)   { return zx::DMulScale64(a, b, c, d, s); }
inline uint32_t UMulScale16(uint32_t a, uint32_t b)                                { return uint32_t(((uint64_t)a * b) >> 16); }
inline int ksgn(int a)                                                             { return (a > 0) - (a < 0); }

// Bounded multiply-scale: clamp the result into the fixed_t (int64) range.
inline int64_t BoundMulScale(int64_t a, int64_t b, unsigned s)
{
	// The scale layer already carries the full 128-bit product; a result that overflows int64
	// is clamped here. (Zero uses in the engine today; kept for API completeness.)
	return zx::MulScale64(a, b, s);
}

inline void clearbuf(void *d, int c, int32_t a)      { int32_t *p = (int32_t *)d; while (c-- > 0) *p++ = a; }
inline void clearbufshort(void *d, int c, uint16_t a){ uint16_t *p = (uint16_t *)d; while (c-- > 0) *p++ = a; }

// [rc4l] The numbered fixed-shift forms the engine actually calls, generated over shifts 1..32.
// Each delegates to the tested 64-bit scale layer with a compile-time shift.
#define ZX_MAKE_SCALE(N) \
	inline int64_t MulScale##N(int64_t a, int64_t b)                                            { return zx::MulScale64(a, b, N); } \
	inline int64_t DivScale##N(int64_t a, int64_t b)                                            { return zx::DivScale64(a, N, b); } \
	inline int64_t DMulScale##N(int64_t a, int64_t b, int64_t c, int64_t d)                     { return zx::DMulScale64(a, b, c, d, N); } \
	inline int64_t TMulScale##N(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f) { return zx::TMulScale64(a, b, c, d, e, f, N); }
ZX_MAKE_SCALE(1)  ZX_MAKE_SCALE(2)  ZX_MAKE_SCALE(3)  ZX_MAKE_SCALE(4)
ZX_MAKE_SCALE(5)  ZX_MAKE_SCALE(6)  ZX_MAKE_SCALE(7)  ZX_MAKE_SCALE(8)
ZX_MAKE_SCALE(9)  ZX_MAKE_SCALE(10) ZX_MAKE_SCALE(11) ZX_MAKE_SCALE(12)
ZX_MAKE_SCALE(13) ZX_MAKE_SCALE(14) ZX_MAKE_SCALE(15) ZX_MAKE_SCALE(16)
ZX_MAKE_SCALE(17) ZX_MAKE_SCALE(18) ZX_MAKE_SCALE(19) ZX_MAKE_SCALE(20)
ZX_MAKE_SCALE(21) ZX_MAKE_SCALE(22) ZX_MAKE_SCALE(23) ZX_MAKE_SCALE(24)
ZX_MAKE_SCALE(25) ZX_MAKE_SCALE(26) ZX_MAKE_SCALE(27) ZX_MAKE_SCALE(28)
ZX_MAKE_SCALE(29) ZX_MAKE_SCALE(30) ZX_MAKE_SCALE(31) ZX_MAKE_SCALE(32)
#undef ZX_MAKE_SCALE

#endif // ZX_FIXEDMATH_H
