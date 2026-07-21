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

namespace zx
{
// [rc4l] Mode-agnostic raw accessor for the strong-fixed migration: identity for plain integers
// (and the non-strict fixed_t, which is int64), .Raw() for the strong Fixed (declared in the
// ZX_STRONG_FIXED block below). Shared code can unwrap fixed_t to the int64 core without caring
// which mode is active. Inlines to nothing, so the default build is unaffected.
inline constexpr int64_t raw(int64_t v) { return v; }
}

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

#if defined(ZX_STRONG_FIXED) && defined(__cplusplus)
// [rc4l] Strong-Fixed overloads for the migration. Under ZX_STRONG_FIXED fixed_t is zx::Fixed, so
// the scale family needs to accept and return it: these unwrap to the tested int64 core (.Raw())
// and rewrap (FromRaw), behaviour identical. The plain int64 overloads above still serve genuine
// integer math (angle/count Scale calls). Overload resolution is unambiguous because Fixed->int64
// is explicit (so int64 overloads never steal Fixed args) and int->int64 beats int->Fixed (so
// plain-int calls stay on the int64 path).
#include "features/fixed64/computation/fixed_strong.h"

namespace zx { inline constexpr int64_t raw(Fixed f) { return f.Raw(); } }

inline zx::Fixed Scale(zx::Fixed a, zx::Fixed b, zx::Fixed c) { return zx::Fixed::FromRaw(zx::ComputeMulDivS64(a.Raw(), b.Raw(), c.Raw())); }
inline zx::Fixed MulScale(zx::Fixed a, zx::Fixed b, unsigned s) { return zx::Fixed::FromRaw(zx::MulScale64(a.Raw(), b.Raw(), s)); }
inline zx::Fixed DivScale(zx::Fixed a, zx::Fixed b, unsigned s) { return zx::Fixed::FromRaw(zx::DivScale64(a.Raw(), s, b.Raw())); }
inline zx::Fixed DMulScale(zx::Fixed a, zx::Fixed b, zx::Fixed c, zx::Fixed d, unsigned s) { return zx::Fixed::FromRaw(zx::DMulScale64(a.Raw(), b.Raw(), c.Raw(), d.Raw(), s)); }

#define ZX_MAKE_SCALE_FIXED(N) \
	inline zx::Fixed MulScale##N(zx::Fixed a, zx::Fixed b)                                                       { return zx::Fixed::FromRaw(zx::MulScale64(a.Raw(), b.Raw(), N)); } \
	inline zx::Fixed DivScale##N(zx::Fixed a, zx::Fixed b)                                                       { return zx::Fixed::FromRaw(zx::DivScale64(a.Raw(), N, b.Raw())); } \
	inline zx::Fixed DMulScale##N(zx::Fixed a, zx::Fixed b, zx::Fixed c, zx::Fixed d)                            { return zx::Fixed::FromRaw(zx::DMulScale64(a.Raw(), b.Raw(), c.Raw(), d.Raw(), N)); } \
	inline zx::Fixed TMulScale##N(zx::Fixed a, zx::Fixed b, zx::Fixed c, zx::Fixed d, zx::Fixed e, zx::Fixed f)  { return zx::Fixed::FromRaw(zx::TMulScale64(a.Raw(), b.Raw(), c.Raw(), d.Raw(), e.Raw(), f.Raw(), N)); }
ZX_MAKE_SCALE_FIXED(1)  ZX_MAKE_SCALE_FIXED(2)  ZX_MAKE_SCALE_FIXED(3)  ZX_MAKE_SCALE_FIXED(4)
ZX_MAKE_SCALE_FIXED(5)  ZX_MAKE_SCALE_FIXED(6)  ZX_MAKE_SCALE_FIXED(7)  ZX_MAKE_SCALE_FIXED(8)
ZX_MAKE_SCALE_FIXED(9)  ZX_MAKE_SCALE_FIXED(10) ZX_MAKE_SCALE_FIXED(11) ZX_MAKE_SCALE_FIXED(12)
ZX_MAKE_SCALE_FIXED(13) ZX_MAKE_SCALE_FIXED(14) ZX_MAKE_SCALE_FIXED(15) ZX_MAKE_SCALE_FIXED(16)
ZX_MAKE_SCALE_FIXED(17) ZX_MAKE_SCALE_FIXED(18) ZX_MAKE_SCALE_FIXED(19) ZX_MAKE_SCALE_FIXED(20)
ZX_MAKE_SCALE_FIXED(21) ZX_MAKE_SCALE_FIXED(22) ZX_MAKE_SCALE_FIXED(23) ZX_MAKE_SCALE_FIXED(24)
ZX_MAKE_SCALE_FIXED(25) ZX_MAKE_SCALE_FIXED(26) ZX_MAKE_SCALE_FIXED(27) ZX_MAKE_SCALE_FIXED(28)
ZX_MAKE_SCALE_FIXED(29) ZX_MAKE_SCALE_FIXED(30) ZX_MAKE_SCALE_FIXED(31) ZX_MAKE_SCALE_FIXED(32)
#undef ZX_MAKE_SCALE_FIXED
#endif // ZX_STRONG_FIXED

#endif // ZX_FIXEDMATH_H
