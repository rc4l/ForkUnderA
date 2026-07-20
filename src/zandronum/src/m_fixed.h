// [rc4l] Fixed-point helpers for ZandroX.
//
// The core scale math lives in computation/fixedmath.h (clean-room, unit-tested).
// This header adds the engine-facing wrappers (overflow-clamped division, the
// FixedMul/FixedDiv aliases, and the float/angle conversion macros). None of it is
// derived from Ken Silverman's Build "pragmas.h" — ZandroX carries no BUILD-licensed
// code. The 32-bit-x86 hand-written-assembly variants have been dropped (all supported
// targets are 64-bit and use the portable 64-bit-intermediate math).
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef __M_FIXED__
#define __M_FIXED__

#include <stdlib.h>
#include <cstdint>
#include "doomtype.h"
#include "computation/fixedmath.h"
#include "xs_Float.h"

// [rc4l] Fixed-point division (widened to 64-bit fixed_t) that treats a zero divisor as an
// overflow rather than crashing. The (a<<x)/b math is the tested zx::DivScale64 (with its
// 32-bit fast-path and 128-bit wide path); a valid fixed_t result is already within
// FIXED_MIN/FIXED_MAX, so no post-clamp is needed.
#define ZX_MAKE_SAFEDIVSCALE(x) \
	inline fixed_t SafeDivScale##x (fixed_t a, fixed_t b) \
	{ \
		if (b == 0) return (a < 0) ? FIXED_MIN : FIXED_MAX; \
		return zx::DivScale64(a, (x), b); \
	}
ZX_MAKE_SAFEDIVSCALE(1)
ZX_MAKE_SAFEDIVSCALE(2)
ZX_MAKE_SAFEDIVSCALE(3)
ZX_MAKE_SAFEDIVSCALE(4)
ZX_MAKE_SAFEDIVSCALE(5)
ZX_MAKE_SAFEDIVSCALE(6)
ZX_MAKE_SAFEDIVSCALE(7)
ZX_MAKE_SAFEDIVSCALE(8)
ZX_MAKE_SAFEDIVSCALE(9)
ZX_MAKE_SAFEDIVSCALE(10)
ZX_MAKE_SAFEDIVSCALE(11)
ZX_MAKE_SAFEDIVSCALE(12)
ZX_MAKE_SAFEDIVSCALE(13)
ZX_MAKE_SAFEDIVSCALE(14)
ZX_MAKE_SAFEDIVSCALE(15)
ZX_MAKE_SAFEDIVSCALE(16)
ZX_MAKE_SAFEDIVSCALE(17)
ZX_MAKE_SAFEDIVSCALE(18)
ZX_MAKE_SAFEDIVSCALE(19)
ZX_MAKE_SAFEDIVSCALE(20)
ZX_MAKE_SAFEDIVSCALE(21)
ZX_MAKE_SAFEDIVSCALE(22)
ZX_MAKE_SAFEDIVSCALE(23)
ZX_MAKE_SAFEDIVSCALE(24)
ZX_MAKE_SAFEDIVSCALE(25)
ZX_MAKE_SAFEDIVSCALE(26)
ZX_MAKE_SAFEDIVSCALE(27)
ZX_MAKE_SAFEDIVSCALE(28)
ZX_MAKE_SAFEDIVSCALE(29)
ZX_MAKE_SAFEDIVSCALE(30)
ZX_MAKE_SAFEDIVSCALE(31)
ZX_MAKE_SAFEDIVSCALE(32)
#undef ZX_MAKE_SAFEDIVSCALE

#define FixedMul MulScale16
#define FixedDiv SafeDivScale16

// Write count fixed-point values stepping by delta, taking the integer (>>16) part.
inline void qinterpolatedown16 (SDWORD *out, DWORD count, fixed_t val, fixed_t delta)
{
	for (DWORD i = 0; i < count; i++) { out[i] = (SDWORD)(val >> 16); val += delta; }
}

inline void qinterpolatedown16short (short *out, DWORD count, fixed_t val, fixed_t delta)
{
	for (DWORD i = 0; i < count; i++) { out[i] = (short)(val >> 16); val += delta; }
}

	//returns num/den, dmval = num%den
inline SDWORD DivMod (SDWORD num, SDWORD den, SDWORD *dmval)
{
	*dmval = num % den;
	return num / den;
}

	//returns num%den, dmval = num/den
inline SDWORD ModDiv (SDWORD num, SDWORD den, SDWORD *dmval)
{
	*dmval = num / den;
	return num % den;
}

#define FLOAT2FIXED(f)		xs_Fix<16>::ToFix(f)
#define FIXED2FLOAT(f)		((f) / float(65536))
#define FIXED2DBL(f)		((f) / double(65536))

#define ANGLE2DBL(f)		((f) * (90./ANGLE_90))
#define ANGLE2FLOAT(f)		(float((f) * (90./ANGLE_90)))
#define FLOAT2ANGLE(f)		((angle_t)xs_CRoundToInt((f) * (ANGLE_90/90.)))

#define ANGLE2RAD(f)		((f) * (M_PI/ANGLE_180))
#define ANGLE2RADF(f)		((f) * float(M_PI/ANGLE_180))
#define RAD2ANGLE(f)		((angle_t)xs_CRoundToInt((f) * (ANGLE_180/M_PI)))

#endif
