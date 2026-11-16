// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] fua_Vec: the vector operations zt-bcc compiles vec2 and vec3 code to, exact in integers so every machine gives the same bits.

#ifndef ZX_ACSVEC_COMPUTE_H
#define ZX_ACSVEC_COMPUTE_H

#include <cstdint>

namespace zx { namespace acsvec {

// [Doruk-Aksoy] The ACS function number, after the actor material ones; compiled into mods, it never moves.
enum { kVecFunction = 24012 };

// [Doruk-Aksoy] The operations, the last argument of a call; zt-bcc's src/fua_vec.h holds the same numbers, and both pin them.
enum VecOp
{
	kOpAbiVersion = 0,
	kOpLength2 = 1,
	kOpLength3 = 2,
	kOpUnit2 = 3,
	kOpUnit3 = 4,
	kOpDot2 = 5,
	kOpDot3 = 6,
	kOpCross3 = 7,
	kOpCount
};

// [Doruk-Aksoy] What kOpAbiVersion returns; a compiled mod falls back to its own code on anything lower than it needs.
enum { kAbiVersion = 1 };

// [Doruk-Aksoy] The largest result magnitude, 32767.99998 in 16.16.
constexpr int32_t kVecMax = 0x7FFFFFFF;

// [Doruk-Aksoy] A call's results: the first is returned, the rest go to cells 0 onward of the caller's channel array.
struct VecCall
{
	int count;          // [Doruk-Aksoy] 0 when the call is refused
	int channel;        // [Doruk-Aksoy] the map variable holding the caller's channel array
	int32_t value[3];
};

// [Doruk-Aksoy] fua_Vec(operands..., int channel, int op), or fua_Vec(kOpAbiVersion) alone.
VecCall ComputeVecCall(const int32_t* args, int argCount);

// [Doruk-Aksoy] The operands an operation takes, or -1 for an unknown one.
int ComputeVecOperandCount(int op);

// [Doruk-Aksoy] The length of `count` 16.16 components, rounded to the nearest 1/65536 and saturated.
int32_t ComputeVecLength(const int32_t* v, int count);

// [Doruk-Aksoy] The unit vector of `count` components, each rounded to the nearest 1/65536; a zero vector stays zero.
void ComputeVecUnit(const int32_t* v, int count, int32_t* out);

// [Doruk-Aksoy] The dot product of two `count`-component vectors, rounded and saturated.
int32_t ComputeVecDot(const int32_t* a, const int32_t* b, int count);

// [Doruk-Aksoy] The cross product of two 3-component vectors, each component rounded and saturated.
void ComputeVecCross(const int32_t* a, const int32_t* b, int32_t* out);

// [Doruk-Aksoy] round(sqrt(s)), exactly.
uint64_t ComputeVecRoundSqrt(uint64_t s);

// [Doruk-Aksoy] The sum of signed products a[i] * b[i] * sign[i], divided by 65536, rounded half away from zero and saturated.
int32_t ComputeVecSumProducts(const int32_t* a, const int32_t* b, const int* sign, int count);

}} // namespace zx::acsvec

#endif
