// [rc4l] Verifies the 128-bit primitives, especially the software 128/64 divide that only
// runs on ARM64 MSVC. Everything is checked against the native __int128 reference, so the
// MSVC-only code is proven correct here on clang. A deterministic LCG drives the random
// vectors so the run is reproducible (no wall-clock/rand seeding).
#include "gtest/gtest.h"
#include "features/fixed64/computation/wide128_compute.h"
#include <cstdint>

namespace
{
// [rc4l] Small reproducible generator; avoids <random> nondeterminism across libstdc++/libc++.
struct Lcg
{
	uint64_t s;
	explicit Lcg(uint64_t seed) : s(seed) {}
	uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s; }
	int64_t range(int64_t lo, int64_t hi) { return lo + (int64_t)(next() % (uint64_t)(hi - lo + 1)); }
};

// [rc4l] The 128/64 software divide across the full dividend range (quotient truncated to
// low 64), compared to the native reference. Includes hi==0 (fast path) and hi!=0.
TEST(Wide128, UDiv128SoftMatchesReferenceRandom)
{
	Lcg r(0x1234567890abcdefULL);
	for (int i = 0; i < 20000; ++i)
	{
		const uint64_t hi = (i % 4 == 0) ? 0 : r.next();   // exercise the hi==0 fast path too
		const uint64_t lo = r.next();
		uint64_t d = r.next();
		if (d == 0) d = 1;
		const unsigned __int128 num = ((unsigned __int128)hi << 64) | lo;
		const uint64_t expect = (uint64_t)(num / d);
		EXPECT_EQ(zx::ComputeUDiv128Soft(hi, lo, d), expect) << "hi=" << hi << " lo=" << lo << " d=" << d;
	}
}

// [rc4l] The carry-out path: a large divisor forces (rem<<1) to overflow 64 bits, which the
// explicit carry handles. Big dividend / big divisor stresses exactly that branch.
TEST(Wide128, UDiv128SoftHandlesLargeDivisorCarry)
{
	Lcg r(0xfeedbeefcafef00dULL);
	for (int i = 0; i < 5000; ++i)
	{
		const uint64_t hi = r.next();
		const uint64_t lo = r.next();
		const uint64_t d = r.next() | 0x8000000000000000ULL;   // top bit set: near-2^64 divisor
		const unsigned __int128 num = ((unsigned __int128)hi << 64) | lo;
		EXPECT_EQ(zx::ComputeUDiv128Soft(hi, lo, d), (uint64_t)(num / d));
	}
}

TEST(Wide128, UMul128SoftMatchesReference)
{
	Lcg r(0x0f1e2d3c4b5a6978ULL);
	for (int i = 0; i < 20000; ++i)
	{
		const uint64_t a = r.next(), b = r.next();
		uint64_t hi;
		const uint64_t lo = zx::ComputeUMul128Soft(a, b, &hi);
		const unsigned __int128 ref = (unsigned __int128)a * b;
		EXPECT_EQ(lo, (uint64_t)ref);
		EXPECT_EQ(hi, (uint64_t)(ref >> 64));
	}
	// Edge magnitudes.
	uint64_t hi;
	EXPECT_EQ(zx::ComputeUMul128Soft(0, 12345, &hi), 0u); EXPECT_EQ(hi, 0u);
	EXPECT_EQ(zx::ComputeUMul128Soft(~0ULL, ~0ULL, &hi), 1u); EXPECT_EQ(hi, ~0ULL - 1);
}

// [rc4l] Signed multiply-shift: both the public (native) and software paths must match the
// __int128 reference. Operands bounded so the result fits int64.
TEST(Wide128, MulShiftMatchesReferenceAllSignsAndShifts)
{
	Lcg r(0xa5a5a5a5a5a5a5a5ULL);
	for (int i = 0; i < 40000; ++i)
	{
		const int64_t a = r.range(-(1LL << 40), 1LL << 40);
		const int64_t b = r.range(-(1LL << 40), 1LL << 40);
		const unsigned shift = (unsigned)(r.next() % 41);   // 0..40, includes shift==0
		const int64_t ref = (int64_t)(((__int128)a * b) >> shift);
		EXPECT_EQ(zx::ComputeMulShiftS64Soft(a, b, shift), ref) << "a=" << a << " b=" << b << " s=" << shift;
		EXPECT_EQ(zx::ComputeMulShiftS64(a, b, shift), ref);
	}
	// Explicit sign-quadrant + shift==0 coverage.
	EXPECT_EQ(zx::ComputeMulShiftS64Soft(7, 9, 0), 63);
	EXPECT_EQ(zx::ComputeMulShiftS64Soft(-7, 9, 1), (int64_t)(((__int128)-7 * 9) >> 1));
	EXPECT_EQ(zx::ComputeMulShiftS64Soft(7, -9, 2), (int64_t)(((__int128)7 * -9) >> 2));
	EXPECT_EQ(zx::ComputeMulShiftS64Soft(-7, -9, 3), (int64_t)(((__int128)-7 * -9) >> 3));
	// [rc4l] Negative product whose low 64 bits are zero (|product| == 2^64), to exercise the
	// two's-complement carry into the high word (the lo == 0 branch of the negation).
	EXPECT_EQ(zx::ComputeMulShiftS64Soft(-(1LL << 40), (1LL << 24), 16),
		(int64_t)(((__int128)-(1LL << 40) * (1LL << 24)) >> 16));
}

// [rc4l] Signed divide-shift, both paths vs reference. Operands bounded so the true quotient
// fits int64 (b != 0 by contract).
TEST(Wide128, DivShiftMatchesReferenceAllSignsAndShifts)
{
	Lcg r(0x5c5c5c5c5c5c5c5cULL);
	for (int i = 0; i < 40000; ++i)
	{
		const int64_t a = r.range(-(1LL << 28), 1LL << 28);
		const unsigned shift = (unsigned)(r.next() % 31);   // 0..30, includes shift==0
		int64_t b = r.range(-(1LL << 20), 1LL << 20);
		if (b == 0) b = 1;
		// [rc4l] a can be negative and left-shifting a negative is UB, so scale by multiply.
		const int64_t ref = (int64_t)(((__int128)a * ((__int128)1 << shift)) / b);
		EXPECT_EQ(zx::ComputeDivShiftS64Soft(a, shift, b), ref) << "a=" << a << " s=" << shift << " b=" << b;
		EXPECT_EQ(zx::ComputeDivShiftS64(a, shift, b), ref);
	}
	// Explicit sign-quadrant + shift==0 coverage. Scale by multiply, not a negative shift (UB).
	EXPECT_EQ(zx::ComputeDivShiftS64Soft(100, 0, 7), 14);
	EXPECT_EQ(zx::ComputeDivShiftS64Soft(-100, 4, 7), (int64_t)(((__int128)-100 * 16) / 7));
	EXPECT_EQ(zx::ComputeDivShiftS64Soft(100, 4, -7), (int64_t)(((__int128)100 * 16) / -7));
	EXPECT_EQ(zx::ComputeDivShiftS64Soft(-100, 4, -7), (int64_t)(((__int128)-100 * 16) / -7));
}
} // namespace
