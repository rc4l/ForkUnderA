// [rc4l] Verifies the 64-bit scale layer: both the 32-bit fast-path and the wide128 path
// against the __int128 reference, and that both branches are exercised. Deterministic LCG.
#include "gtest/gtest.h"
#include "features/fixed64/computation/fixed64_scale_compute.h"
#include <cstdint>

namespace
{
struct Lcg
{
	uint64_t s;
	explicit Lcg(uint64_t seed) : s(seed) {}
	uint64_t next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return s; }
	int64_t range(int64_t lo, int64_t hi) { return lo + (int64_t)(next() % (uint64_t)(hi - lo + 1)); }
};

int64_t MulRef(int64_t a, int64_t b, unsigned s) { return (int64_t)(((__int128)a * b) >> s); }
int64_t DivRef(int64_t a, unsigned s, int64_t b) { return (int64_t)(((__int128)a * ((__int128)1 << s)) / b); }

// [rc4l] Fast-path regime: both operands fit int32, so the branch stays on the 64-bit path.
TEST(Fixed64Scale, MulFastPathMatchesReference)
{
	Lcg r(0x11aa22bb33cc44ddULL);
	for (int i = 0; i < 40000; ++i)
	{
		const int64_t a = r.range(INT32_MIN, INT32_MAX);
		const int64_t b = r.range(INT32_MIN, INT32_MAX);
		const unsigned s = (unsigned)(r.next() % 33);
		EXPECT_EQ(zx::MulScale64(a, b, s), MulRef(a, b, s)) << "a=" << a << " b=" << b << " s=" << s;
	}
	EXPECT_EQ(zx::Fixed64Mul(3 << 16, 5 << 16), (int64_t)(3 * 5) << 16);  // fast path
}

// [rc4l] Wide regime: at least one operand exceeds int32, forcing the wide128 branch.
TEST(Fixed64Scale, MulWidePathMatchesReference)
{
	Lcg r(0x55ee66ff7788990aULL);
	for (int i = 0; i < 40000; ++i)
	{
		// At least one operand out of int32 range so Fits32 is false -> wide path.
		const int64_t a = r.range(-(1LL << 44), 1LL << 44);
		const int64_t b = r.range(-(1LL << 20), 1LL << 20);
		const unsigned s = (unsigned)(r.next() % 41);
		EXPECT_EQ(zx::MulScale64(a, b, s), MulRef(a, b, s)) << "a=" << a << " b=" << b << " s=" << s;
	}
	// Explicit giant operand -> proves the wide branch is taken and correct. Both orders, so
	// the compound Fits32(a) && Fits32(b) is exercised with each operand being the one that
	// fails (a-fails short-circuits; a-fits-b-fails needs the second term evaluated false).
	const int64_t big = (1LL << 40);
	EXPECT_EQ(zx::MulScale64(big, 7, 16), MulRef(big, 7, 16));
	EXPECT_EQ(zx::MulScale64(7, big, 16), MulRef(7, big, 16));
}

// [rc4l] Div fast-path: |a| fits int32 and shift <= 31.
TEST(Fixed64Scale, DivFastPathMatchesReference)
{
	Lcg r(0x0badf00dfeed1234ULL);
	for (int i = 0; i < 40000; ++i)
	{
		const int64_t a = r.range(INT32_MIN, INT32_MAX);
		const unsigned s = (unsigned)(r.next() % 32);   // 0..31
		int64_t b = r.range(-(1LL << 24), 1LL << 24);
		if (b == 0) b = 1;
		EXPECT_EQ(zx::DivScale64(a, s, b), DivRef(a, s, b)) << "a=" << a << " s=" << s << " b=" << b;
	}
	EXPECT_EQ(zx::Fixed64Div(10 << 16, 2 << 16), (int64_t)(10 << 16) / 2);  // fast path
}

// [rc4l] Div wide path: triggered by a not fitting int32, AND separately by shift > 31.
TEST(Fixed64Scale, DivWidePathMatchesReference)
{
	Lcg r(0xdeadbeef12345678ULL);
	for (int i = 0; i < 40000; ++i)
	{
		const int64_t a = r.range(-(1LL << 44), 1LL << 44);   // exceeds int32 -> wide
		const unsigned s = (unsigned)(r.next() % 20);
		int64_t b = r.range(-(1LL << 24), 1LL << 24);
		if (b == 0) b = 1;
		EXPECT_EQ(zx::DivScale64(a, s, b), DivRef(a, s, b)) << "a=" << a << " s=" << s << " b=" << b;
	}
	// shift > 31 with a small operand also takes the wide branch.
	EXPECT_EQ(zx::DivScale64(1000, 40, 7), DivRef(1000, 40, 7));
	EXPECT_EQ(zx::DivScale64(-1000, 40, 7), DivRef(-1000, 40, 7));
}

// [rc4l] Fast and wide paths must agree at the boundary (a value computed both ways).
TEST(Fixed64Scale, FastAndWideAgreeAtBoundary)
{
	for (int64_t a = INT32_MAX - 3; a <= (int64_t)INT32_MAX + 3; ++a)
	{
		const int64_t b = 12345;
		// MulScale64 auto-selects; ComputeMulShiftS64 is always the wide path. Same answer.
		EXPECT_EQ(zx::MulScale64(a, b, 16), zx::ComputeMulShiftS64(a, b, 16)) << "a=" << a;
	}
}
} // namespace
