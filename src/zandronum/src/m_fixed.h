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

// [rc4l] SafeDivScale and the FixedMul/FixedDiv aliases live in computation/fixedmath.h, so compute code reaches them without the engine headers.

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
// [rc4l] float(f) works in both modes: (float)int64 in the plain build, and Fixed's explicit
// operator float in the strong-fixed build. So these need no per-mode variant.
#define FIXED2FLOAT(f)		(float(f) / 65536.0f)
#define FIXED2DBL(f)		(double(f) / 65536.0)

#define ANGLE2DBL(f)		((f) * (90./ANGLE_90))
#define ANGLE2FLOAT(f)		(float((f) * (90./ANGLE_90)))
#define FLOAT2ANGLE(f)		((angle_t)xs_CRoundToInt((f) * (ANGLE_90/90.)))

#define ANGLE2RAD(f)		((f) * (M_PI/ANGLE_180))
#define ANGLE2RADF(f)		((f) * float(M_PI/ANGLE_180))
#define RAD2ANGLE(f)		((angle_t)xs_CRoundToInt((f) * (ANGLE_180/M_PI)))

#endif
