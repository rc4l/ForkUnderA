// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acsvec/computation/acsvec_compute.h"

namespace zx { namespace acsvec {

namespace {

// [Doruk-Aksoy] The full 128-bit product of two 64-bit values, as high and low halves.
struct Wide
{
	uint64_t hi;
	uint64_t lo;
};

Wide Multiply(uint64_t a, uint64_t b)
{
	const uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
	const uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
	const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
	const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
	return Wide { p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (p00 & 0xFFFFFFFFu) | (mid << 32) };
}

bool NotAbove(Wide a, Wide b)
{
	return a.hi < b.hi || (a.hi == b.hi && a.lo <= b.lo);
}

int32_t Saturate(int64_t v)
{
	return v > kVecMax ? kVecMax : v < -kVecMax ? -kVecMax : static_cast<int32_t>(v);
}

uint64_t SumOfSquares(const int32_t* v, int count)
{
	uint64_t s = 0;
	for (int i = 0; i < count; ++i)
		s += static_cast<uint64_t>(static_cast<int64_t>(v[i]) * v[i]);
	return s;
}

uint64_t Magnitude(int32_t v)
{
	return v < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(v)) : static_cast<uint64_t>(v);
}

// [Doruk-Aksoy] round(q / sqrt(s)) for q <= sqrt(s) * 65536: the largest n with (2n - 1)^2 * s <= (2q)^2, so a tie rounds up.
int32_t RoundQuotient(uint64_t q, uint64_t s)
{
	const Wide target = Multiply(2 * q, 2 * q);
	int32_t lo = 0, hi = 65536;
	while (lo < hi)
	{
		const int32_t mid = (lo + hi + 1) / 2;
		const uint64_t odd = 2 * static_cast<uint64_t>(mid) - 1;
		if (NotAbove(Multiply(odd * odd, s), target))
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

} // namespace

int ComputeVecOperandCount(int op)
{
	switch (op)
	{
	case kOpAbiVersion: return 0;
	case kOpLength2: case kOpUnit2: return 2;
	case kOpLength3: case kOpUnit3: return 3;
	case kOpDot2: return 4;
	case kOpDot3: case kOpCross3: return 6;
	default: return -1;
	}
}

uint64_t ComputeVecRoundSqrt(uint64_t s)
{
	uint64_t rest = s, root = 0, bit = uint64_t(1) << 62;
	while (bit > rest)
		bit >>= 2;
	while (bit != 0)
	{
		if (rest >= root + bit)
		{
			rest -= root + bit;
			root = (root >> 1) + bit;
		}
		else
			root >>= 1;
		bit >>= 2;
	}
	// [Doruk-Aksoy] s - root^2 is `rest`; s is an integer, so it never sits exactly halfway.
	return rest > root ? root + 1 : root;
}

int32_t ComputeVecLength(const int32_t* v, int count)
{
	const uint64_t r = ComputeVecRoundSqrt(SumOfSquares(v, count));
	return r > static_cast<uint64_t>(kVecMax) ? kVecMax : static_cast<int32_t>(r);
}

void ComputeVecUnit(const int32_t* v, int count, int32_t* out)
{
	const uint64_t s = SumOfSquares(v, count);
	for (int i = 0; i < count; ++i)
	{
		const int32_t n = s == 0 ? 0 : RoundQuotient(Magnitude(v[i]) << 16, s);
		out[i] = v[i] < 0 ? -n : n;
	}
}

int32_t ComputeVecSumProducts(const int32_t* a, const int32_t* b, const int* sign, int count)
{
	// [Doruk-Aksoy] Each product splits into whole and fractional 1/65536ths, so three of them never overflow.
	int64_t whole = 0, frac = 0;
	for (int i = 0; i < count; ++i)
	{
		const int64_t p = static_cast<int64_t>(a[i]) * b[i] * sign[i];
		const int64_t w = p >> 16;
		whole += w;
		frac += p - w * 65536;
	}
	whole += frac >> 16;
	frac &= 0xFFFF;
	return Saturate(whole + (whole >= 0 ? frac >= 32768 : frac > 32768));
}

int32_t ComputeVecDot(const int32_t* a, const int32_t* b, int count)
{
	static const int plus[3] = { 1, 1, 1 };
	return ComputeVecSumProducts(a, b, plus, count);
}

void ComputeVecCross(const int32_t* a, const int32_t* b, int32_t* out)
{
	static const int sign[2] = { 1, -1 };
	for (int axis = 0; axis < 3; ++axis)
	{
		const int j = (axis + 1) % 3, k = (axis + 2) % 3;
		const int32_t left[2] = { a[j], a[k] };
		const int32_t right[2] = { b[k], b[j] };
		out[axis] = ComputeVecSumProducts(left, right, sign, 2);
	}
}

VecCall ComputeVecCall(const int32_t* args, int argCount)
{
	VecCall call = { 0, 0, { 0, 0, 0 } };
	if (argCount < 1)
		return call;
	const int op = args[argCount - 1];
	if (op == kOpAbiVersion)
	{
		call.count = 1;
		call.value[0] = kAbiVersion;
		return call;
	}
	const int operands = ComputeVecOperandCount(op);
	if (operands < 0 || argCount != operands + 2)
		return call;
	call.channel = args[argCount - 2];
	switch (op)
	{
	case kOpLength2:
	case kOpLength3:
		call.count = 1;
		call.value[0] = ComputeVecLength(args, operands);
		break;
	case kOpUnit2:
	case kOpUnit3:
		call.count = operands;
		ComputeVecUnit(args, operands, call.value);
		break;
	case kOpDot2:
	case kOpDot3:
		call.count = 1;
		call.value[0] = ComputeVecDot(args, args + operands / 2, operands / 2);
		break;
	default:
		call.count = 3;
		ComputeVecCross(args, args + 3, call.value);
		break;
	}
	return call;
}

}} // namespace zx::acsvec
