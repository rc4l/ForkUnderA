// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] xs_Float.h's conversions without its header: the magic add that leaves a double rounded in its low word, as GL's angles and FLOAT2FIXED take it.

#ifndef ZX_XSROUND_COMPUTE_H
#define ZX_XSROUND_COMPUTE_H

#include <string.h>

namespace zx {

// [rc4l] xs_CRoundToInt(val, magic): val plus the magic constant, the low 32 bits of the double.
inline int ComputeXsRound(double val, double magic)
{
	const double sum = val + magic;
	unsigned long long bits;
	memcpy(&bits, &sum, sizeof(bits));
	return (int)(unsigned)bits;
}

// [rc4l] xs_Fix<shift>::ToFix: val in fixed point with `shift` fraction bits, rounded to nearest with ties to even.
inline int ComputeXsFix(double val, int shift)
{
	return ComputeXsRound(val, 6755399441055744.0 / double(1u << shift));
}

// [rc4l] xs_FloorToInt: down, by rounding a value nudged just under half a unit lower.
inline int ComputeXsFloor(double val)
{
	return ComputeXsRound(val - (double(.5f) - 1.5e-8), 6755399441055744.0);
}

// [rc4l] xs_RoundToUInt: nearest with ties up (the second add nudges a tie over), the word taken unsigned.
inline unsigned ComputeXsRoundToUInt(double val)
{
	return (unsigned)ComputeXsRound(val + 1.5e-8, 6755399441055744.0);
}

} // namespace zx

#endif // ZX_XSROUND_COMPUTE_H
