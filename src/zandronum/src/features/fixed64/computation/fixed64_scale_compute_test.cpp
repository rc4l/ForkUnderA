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

// [rc4l] DMulScale64/TMulScale64: fast-path (all int32) and wide path, vs reference.
TEST(Fixed64Scale, DMulAndTMulBothPaths)
{
	Lcg r(0x13572468acebdf01ULL);
	for (int i = 0; i < 20000; ++i)
	{
		// Fast regime: all operands fit int32.
		const int64_t a = r.range(INT32_MIN, INT32_MAX), b = r.range(-30000, 30000);
		const int64_t c = r.range(INT32_MIN, INT32_MAX), d = r.range(-30000, 30000);
		const unsigned s = (unsigned)(r.next() % 33);
		EXPECT_EQ(zx::DMulScale64(a, b, c, d, s),
			(int64_t)((((__int128)a * b) + ((__int128)c * d)) >> s));
		// Wide regime: a giant operand forces the 128-bit path.
		const int64_t big = 1LL << 40;
		EXPECT_EQ(zx::DMulScale64(big, b, c, d, 16),
			(int64_t)((((__int128)big * b) + ((__int128)c * d)) >> 16));
		EXPECT_EQ(zx::TMulScale64(a, b, c, d, big, 3, 16),
			(int64_t)((((__int128)a * b) + ((__int128)c * d) + ((__int128)big * 3)) >> 16));
	}
	// Fast-path TMul explicit.
	EXPECT_EQ(zx::TMulScale64(2, 3, 4, 5, 6, 7, 0), 2*3 + 4*5 + 6*7);

	// [rc4l] Each operand position independently forces the wide path, so every term of the
	// short-circuit Fits32(...) && ... chain is exercised as the one that fails.
	const int64_t B = 1LL << 40, s = 12345;
	auto dref = [](int64_t a, int64_t b, int64_t c, int64_t d) {
		return (int64_t)((((__int128)a * b) + ((__int128)c * d)) >> 16); };
	EXPECT_EQ(zx::DMulScale64(B, s, s, s, 16), dref(B, s, s, s));  // a fails
	EXPECT_EQ(zx::DMulScale64(s, B, s, s, 16), dref(s, B, s, s));  // b fails
	EXPECT_EQ(zx::DMulScale64(s, s, B, s, 16), dref(s, s, B, s));  // c fails
	EXPECT_EQ(zx::DMulScale64(s, s, s, B, 16), dref(s, s, s, B));  // d fails
	auto tref = [](int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f) {
		return (int64_t)((((__int128)a*b)+((__int128)c*d)+((__int128)e*f)) >> 16); };
	EXPECT_EQ(zx::TMulScale64(B, s, s, s, s, s, 16), tref(B, s, s, s, s, s));  // a
	EXPECT_EQ(zx::TMulScale64(s, B, s, s, s, s, 16), tref(s, B, s, s, s, s));  // b
	EXPECT_EQ(zx::TMulScale64(s, s, B, s, s, s, 16), tref(s, s, B, s, s, s));  // c
	EXPECT_EQ(zx::TMulScale64(s, s, s, B, s, s, 16), tref(s, s, s, B, s, s));  // d
	EXPECT_EQ(zx::TMulScale64(s, s, s, s, B, s, 16), tref(s, s, s, s, B, s));  // e
	EXPECT_EQ(zx::TMulScale64(s, s, s, s, s, B, 16), tref(s, s, s, s, s, B));  // f
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

// [rc4l] Mul32Wrap must reproduce the 32-bit signed-multiply overflow that
// BCOMPATF_SETSLOPEOVERFLOW slopes depend on -- not the full 64-bit product.
TEST(Mul32Wrap, ReproducesThirtyTwoBitOverflow)
{
	const int64_t FRACUNIT = 65536;

	// FRACUNIT * FRACUNIT == 2^32, which wraps to 0 in 32 bits (the whole point of the emulation).
	EXPECT_EQ(zx::Mul32Wrap(FRACUNIT, FRACUNIT), 0);
	EXPECT_NE(zx::Mul32Wrap(FRACUNIT, FRACUNIT), FRACUNIT * FRACUNIT); // != full 64-bit product

	// FRACUNIT * (FRACUNIT/2) == 2^31, which wraps to INT32_MIN as a signed 32-bit value.
	EXPECT_EQ(zx::Mul32Wrap(FRACUNIT, FRACUNIT / 2), INT32_MIN);

	// A negative operand: -FRACUNIT * FRACUNIT == -2^32, wraps to 0.
	EXPECT_EQ(zx::Mul32Wrap(-FRACUNIT, FRACUNIT), 0);
}

// [rc4l] For operands whose product fits int32, Mul32Wrap is an ordinary exact multiply.
TEST(Mul32Wrap, ExactWhenProductFits)
{
	EXPECT_EQ(zx::Mul32Wrap(2, 3), 6);
	EXPECT_EQ(zx::Mul32Wrap(-7, 11), -77);
	EXPECT_EQ(zx::Mul32Wrap(30000, 30000), 900000000); // 9e8 < INT32_MAX, fits, no wrap
}

// [rc4l] AlignDownPow2 must keep the sign of negative values -- the polyobject-rotation bug was a
// 32-bit mask that turned negative rotated coordinates into huge positive ones.
TEST(AlignDownPow2, PreservesSignOnNegatives)
{
	// 512-grid alignment (bits = 9), exactly as RotatePt uses.
	EXPECT_EQ(zx::AlignDownPow2(1000, 9), 512);
	EXPECT_EQ(zx::AlignDownPow2(-1000, 9), -1024); // floor toward -inf, stays negative
	EXPECT_EQ(zx::AlignDownPow2(-1024, 9), -1024); // already aligned
	EXPECT_EQ(zx::AlignDownPow2(-1, 9), -512);
	EXPECT_EQ(zx::AlignDownPow2(0, 9), 0);

	// The pre-fix 32-bit mask turned a negative 64-bit value into a large positive; the helper
	// keeps it negative. This is the exact regression.
	const int64_t neg = -1024;
	EXPECT_GT((neg & 0xFFFFFE00), 0x40000000);   // buggy: ~4.29e9, geometry flung away
	EXPECT_LT(zx::AlignDownPow2(neg, 9), 0);     // fixed: stays negative
}
} // namespace
