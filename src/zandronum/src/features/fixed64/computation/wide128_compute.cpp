// [rc4l] Implementation of the 128-bit intermediate primitives. See wide128_compute.h.
// Standalone; no engine dependencies, so the engine and the test binary both compile it.
#include "features/fixed64/computation/wide128_compute.h"

namespace zx
{

// [rc4l] Unsigned 64x64 -> 128 by splitting each operand into 32-bit halves and summing the
// four partial products with explicit carry. This is the portable fallback the MSVC path
// uses in place of _umul128 / __umulh.
uint64_t ComputeUMul128Soft(uint64_t a, uint64_t b, uint64_t *hi)
{
	const uint64_t aLo = (uint32_t)a, aHi = a >> 32;
	const uint64_t bLo = (uint32_t)b, bHi = b >> 32;

	const uint64_t ll = aLo * bLo;
	const uint64_t lh = aLo * bHi;
	const uint64_t hl = aHi * bLo;
	const uint64_t hh = aHi * bHi;

	// Sum the cross terms into the middle 64 bits, tracking carry into the top.
	const uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
	const uint64_t lo = (uint32_t)ll | (mid << 32);
	*hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
	return lo;
}

// [rc4l] Restoring bitwise long division of the 128-bit (hi:lo) by d, returning the low 64
// bits of the quotient. The remainder invariant rem < d keeps rem in 64 bits; the carry out
// of (rem << 1) is tracked explicitly so a large divisor is handled. O(128) shift-subtract,
// which is fine because the widened path only runs for genuinely out-of-range coordinates.
uint64_t ComputeUDiv128Soft(uint64_t hi, uint64_t lo, uint64_t d)
{
	// [rc4l] Fast path: a 64-bit dividend is a plain divide.
	if (hi == 0)
		return lo / d;

	uint64_t rem = 0;
	uint64_t quo = 0;
	for (int i = 127; i >= 0; --i)
	{
		const uint64_t bit = (i >= 64) ? ((hi >> (i - 64)) & 1u) : ((lo >> i) & 1u);
		// Shift the next dividend bit in; carry is the bit pushed out of the top of rem.
		const uint64_t carry = rem >> 63;
		rem = (rem << 1) | bit;
		if (carry || rem >= d)
		{
			rem -= d;
			if (i < 64)
				quo |= (uint64_t)1 << i;
		}
	}
	return quo;
}

int64_t ComputeMulShiftS64Soft(int64_t a, int64_t b, unsigned shift)
{
	// Multiply magnitudes, then form the two's-complement 128-bit product BEFORE shifting:
	// >> on a signed value is an arithmetic (floor) shift, so the sign must be applied to the
	// full 128-bit value, not to the shifted magnitude (that would truncate toward zero).
	const bool neg = (a < 0) != (b < 0);
	const uint64_t ua = (a < 0) ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
	const uint64_t ub = (b < 0) ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;

	uint64_t hi;
	uint64_t lo = ComputeUMul128Soft(ua, ub, &hi);

	if (neg)
	{
		// Two's complement of the 128-bit (hi:lo).
		lo = ~lo + 1;
		hi = ~hi + (lo == 0 ? 1 : 0);
	}

	// Low 64 bits of the 128-bit arithmetic shift; the discarded high bits are sign extension,
	// and for a result that fits int64 the low 64 bits are the correct signed value.
	const uint64_t res = (shift == 0) ? lo : ((lo >> shift) | (hi << (64 - shift)));
	return (int64_t)res;
}

int64_t ComputeDivShiftS64Soft(int64_t a, unsigned shift, int64_t b)
{
	const bool neg = (a < 0) != (b < 0);
	const uint64_t ua = (a < 0) ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
	const uint64_t ub = (b < 0) ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;

	// Numerator ua << shift as a 128-bit (hi:lo), shift in [0,63].
	const uint64_t hi = (shift == 0) ? 0 : (ua >> (64 - shift));
	const uint64_t lo = ua << shift;

	const uint64_t q = ComputeUDiv128Soft(hi, lo, ub);
	return neg ? -(int64_t)q : (int64_t)q;
}

namespace
{
// [rc4l] Minimal signed 128-bit as two's-complement (hi:lo), for summing products before the
// shift -- (a*b + c*d) >> s must add the full products, not the shifted results.
struct S128 { uint64_t hi, lo; };

S128 MulS128(int64_t a, int64_t b)
{
	const bool neg = (a < 0) != (b < 0);
	const uint64_t ua = (a < 0) ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
	const uint64_t ub = (b < 0) ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;
	S128 r;
	r.lo = ComputeUMul128Soft(ua, ub, &r.hi);
	if (neg)
	{
		r.lo = ~r.lo + 1;
		r.hi = ~r.hi + (r.lo == 0 ? 1 : 0);
	}
	return r;
}

S128 AddS128(S128 x, S128 y)
{
	S128 r;
	r.lo = x.lo + y.lo;
	r.hi = x.hi + y.hi + (r.lo < x.lo ? 1 : 0);   // carry out of the low add
	return r;
}

// [rc4l] Low 64 bits of an arithmetic right shift of the signed 128-bit value. shift in [0,63].
int64_t ShiftS128(S128 v, unsigned shift)
{
	const uint64_t res = (shift == 0) ? v.lo : ((v.lo >> shift) | (v.hi << (64 - shift)));
	return (int64_t)res;
}
} // namespace

int64_t ComputeMulAddShiftS64Soft(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift)
{
	return ShiftS128(AddS128(MulS128(a, b), MulS128(c, d)), shift);
}

int64_t ComputeMulAdd3ShiftS64Soft(int64_t a, int64_t b, int64_t c, int64_t d,
	int64_t e, int64_t f, unsigned shift)
{
	return ShiftS128(AddS128(AddS128(MulS128(a, b), MulS128(c, d)), MulS128(e, f)), shift);
}

int64_t ComputeMulAddShiftS64(int64_t a, int64_t b, int64_t c, int64_t d, unsigned shift)
{
#ifdef __SIZEOF_INT128__
	return (int64_t)((((__int128)a * b) + ((__int128)c * d)) >> shift);
#else
	return ComputeMulAddShiftS64Soft(a, b, c, d, shift);
#endif
}

int64_t ComputeMulAdd3ShiftS64(int64_t a, int64_t b, int64_t c, int64_t d,
	int64_t e, int64_t f, unsigned shift)
{
#ifdef __SIZEOF_INT128__
	return (int64_t)((((__int128)a * b) + ((__int128)c * d) + ((__int128)e * f)) >> shift);
#else
	return ComputeMulAdd3ShiftS64Soft(a, b, c, d, e, f, shift);
#endif
}

int64_t ComputeMulDivS64Soft(int64_t a, int64_t b, int64_t c)
{
	// Truncate toward zero: work on magnitudes, reapply the combined sign.
	const bool neg = (a < 0) != ((b < 0) != (c < 0));
	const uint64_t ua = (a < 0) ? (uint64_t)0 - (uint64_t)a : (uint64_t)a;
	const uint64_t ub = (b < 0) ? (uint64_t)0 - (uint64_t)b : (uint64_t)b;
	const uint64_t uc = (c < 0) ? (uint64_t)0 - (uint64_t)c : (uint64_t)c;

	uint64_t hi;
	const uint64_t lo = ComputeUMul128Soft(ua, ub, &hi);
	const uint64_t q = ComputeUDiv128Soft(hi, lo, uc);
	return neg ? -(int64_t)q : (int64_t)q;
}

int64_t ComputeMulDivS64(int64_t a, int64_t b, int64_t c)
{
#ifdef __SIZEOF_INT128__
	return (int64_t)(((__int128)a * b) / c);
#else
	return ComputeMulDivS64Soft(a, b, c);
#endif
}

// [rc4l] Public dispatch: native __int128 where the compiler has it (clang/gcc, and the
// low-64-truncation matches the software path), otherwise the tested software routines.
int64_t ComputeMulShiftS64(int64_t a, int64_t b, unsigned shift)
{
#ifdef __SIZEOF_INT128__
	return (int64_t)(((__int128)a * (__int128)b) >> shift);
#else
	return ComputeMulShiftS64Soft(a, b, shift);
#endif
}

int64_t ComputeDivShiftS64(int64_t a, unsigned shift, int64_t b)
{
#ifdef __SIZEOF_INT128__
	// [rc4l] Scale by multiply, not a left shift: left-shifting a negative signed value is UB.
	return (int64_t)(((__int128)a * ((__int128)1 << shift)) / (__int128)b);
#else
	return ComputeDivShiftS64Soft(a, shift, b);
#endif
}

} // namespace zx
