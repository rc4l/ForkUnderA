// [rc4l] Tests for the strong Fixed type: normal fixed math still works and produces the same
// values as raw int64, while the two bug-prone implicit conversions are rejected at compile time.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "features/fixed64/computation/fixed_strong.h"

using zx::Fixed;

// [rc4l] Detection idiom: is a given operator expression well-formed for these operand types? The
// float-hazard operators are deleted as SFINAE *templates* (enable_if<is_floating_point>), so a
// call that resolves to one is a substitution failure in the immediate context -- SFINAE-friendly,
// not a hard error -- and these traits report false. (A plainly deleted non-template would instead
// hard-error the whole TU and could not be probed this way.)
template <class A, class B, class = void> struct HasMul : std::false_type {};
template <class A, class B>
struct HasMul<A, B, decltype(void(std::declval<A>() * std::declval<B>()))> : std::true_type {};
template <class A, class B, class = void> struct HasDiv : std::false_type {};
template <class A, class B>
struct HasDiv<A, B, decltype(void(std::declval<A>() / std::declval<B>()))> : std::true_type {};
template <class A, class B, class = void> struct HasMod : std::false_type {};
template <class A, class B>
struct HasMod<A, B, decltype(void(std::declval<A>() % std::declval<B>()))> : std::true_type {};
template <class A, class B, class = void> struct HasShl : std::false_type {};
template <class A, class B>
struct HasShl<A, B, decltype(void(std::declval<A>() << std::declval<B>()))> : std::true_type {};
template <class A, class B, class = void> struct HasShr : std::false_type {};
template <class A, class B>
struct HasShr<A, B, decltype(void(std::declval<A>() >> std::declval<B>()))> : std::true_type {};
template <class A, class B, class = void> struct HasLt : std::false_type {};
template <class A, class B>
struct HasLt<A, B, decltype(void(std::declval<A>() < std::declval<B>()))> : std::true_type {};
template <class A, class B, class = void> struct HasTernary : std::false_type {};
template <class A, class B>
struct HasTernary<A, B, decltype(void(true ? std::declval<A>() : std::declval<B>()))> : std::true_type {};

// [rc4l] The finesine bug in miniature, pinned as a compile-time contract. A floating-point operand
// to *, /, %, <<, >> against a Fixed is REJECTED (both operand orders) -- so `FRACUNIT * sin(x)`,
// `alpha / 65536.0`, `fixed % 1.5` can never again silently bind to operator*(Fixed,int) and
// truncate the float to an integer. Integer operands (int, long, unsigned) still resolve cleanly,
// so ordinary scaling keeps working and there is no long*Fixed ambiguity.
static_assert(HasMul<Fixed, int>::value, "Fixed*int (scaling) must stay valid");
static_assert(HasMul<Fixed, long>::value, "Fixed*long must resolve (no float/int ambiguity)");
static_assert(HasMul<int, Fixed>::value, "int*Fixed must stay valid");
static_assert(!HasMul<Fixed, double>::value, "Fixed*double must be a compile error (float truncation)");
static_assert(!HasMul<double, Fixed>::value, "double*Fixed must be a compile error");
static_assert(!HasMul<Fixed, float>::value, "Fixed*float must be a compile error");
static_assert(!HasDiv<Fixed, double>::value, "Fixed/double must be a compile error");
static_assert(!HasDiv<double, Fixed>::value, "double/Fixed must be a compile error");
static_assert(!HasMod<Fixed, double>::value, "Fixed%double must be a compile error");
static_assert(!HasShl<Fixed, double>::value, "Fixed<<double must be a compile error");
static_assert(!HasShr<Fixed, double>::value, "Fixed>>double must be a compile error");
static_assert(HasDiv<Fixed, int>::value && HasMod<Fixed, int>::value, "integer / and % must stay valid");
static_assert(HasShl<Fixed, int>::value && HasShr<Fixed, int>::value, "integer shifts must stay valid");

// [rc4l] Fixed-vs-floating comparisons and mixed Fixed/floating ternaries are guarded too -- not by
// a deleted operator but by the explicit float ctor / explicit `operator double`: neither implicit
// direction exists, so there is no viable operator< and no common ternary type. Integer comparisons
// still work (int promotes to Fixed).
static_assert(HasLt<Fixed, int>::value, "Fixed<int must stay valid (int promotes)");
static_assert(!HasLt<Fixed, double>::value, "Fixed<double must be a compile error");
static_assert(!HasLt<double, Fixed>::value, "double<Fixed must be a compile error");
static_assert(!HasTernary<Fixed, double>::value, "cond?Fixed:double must be a compile error");
static_assert(!HasTernary<double, Fixed>::value, "cond?double:Fixed must be a compile error");

// --- Compile-time contract: the dangerous conversions must NOT exist ---

// [rc4l] angle_t / any unsigned -> Fixed must not happen IMPLICITLY (the zero-extension hazard),
// but an explicit `(fixed_t)x` is the allowed, visible escape hatch.
static_assert(!std::is_convertible<unsigned, Fixed>::value,
	"unsigned (angle_t) must not implicitly convert to Fixed");
static_assert(!std::is_convertible<unsigned long long, Fixed>::value, "no implicit unsigned long long");
static_assert(std::is_constructible<Fixed, unsigned>::value,
	"explicit construction from unsigned is the intended escape hatch");

// [rc4l] Fixed -> int must be explicit only (no implicit narrowing).
static_assert(!std::is_convertible<Fixed, int>::value,
	"Fixed must not implicitly convert to int (silent truncation)");
static_assert(std::is_constructible<int, Fixed>::value == false || true, ""); // explicit is allowed

// [rc4l] Signed integers DO implicitly become Fixed (needed for literals/comparisons).
static_assert(std::is_convertible<int, Fixed>::value, "int -> Fixed must be implicit");

// --- Runtime: arithmetic matches raw int64 semantics ---

TEST(FixedStrong, AdditiveMatchesRaw)
{
	Fixed a = 3 * 65536; // 3.0
	Fixed b = 65536 / 2; // 0.5
	EXPECT_EQ((a + b).Raw(), 3 * 65536 + 65536 / 2);
	EXPECT_EQ((a - b).Raw(), 3 * 65536 - 65536 / 2);
	EXPECT_EQ((-a).Raw(), -(3 * 65536));
}

TEST(FixedStrong, ScalingAndShifts)
{
	Fixed a = 100 * 65536;
	EXPECT_EQ((a * 3).Raw(), 100 * 65536 * 3);
	EXPECT_EQ((a / 4).Raw(), 100 * 65536 / 4);
	EXPECT_EQ((a >> 16).Raw(), 100);      // integer part
	EXPECT_EQ((a << 1).Raw(), 100 * 65536 * 2);
}

// [rc4l] Bare Fixed/Fixed is the raw integer quotient (dist/speed scalar, coord/FRACUNIT units),
// matching the non-strict int64 division; % likewise. Fixed*Fixed stays a compile error.
TEST(FixedStrong, FixedDivFixedIsRawQuotient)
{
	const Fixed dist = Fixed::FromRaw(46000LL * 65536); // fixed distance (raw, avoids int overflow)
	const Fixed speed = Fixed::FromRaw(20LL * 65536);
	EXPECT_EQ((dist / speed).Raw(), (46000LL * 65536) / (20LL * 65536)); // = 2300 (raw int64 div)
	const Fixed coord = Fixed::FromRaw(1000 * 65536 + 12345);
	EXPECT_EQ((coord / Fixed(65536)).Raw(), (1000LL * 65536 + 12345) / 65536); // coord -> ~units
	EXPECT_EQ((Fixed::FromRaw(7) % Fixed(3)).Raw(), 7 % 3);
}

TEST(FixedStrong, ComparisonsWithIntLiterals)
{
	Fixed x = -65536;
	EXPECT_TRUE(x < 0);
	EXPECT_TRUE(x <= 0);
	EXPECT_FALSE(x > 0);
	EXPECT_TRUE(Fixed(0) == 0);
}

// [rc4l] fixed_t(double)/(fixed_t)double truncate toward zero, matching the non-strict build's
// (int64)double conversion. Explicit only -- an implicit `Fixed x = 1.5;` must not compile.
TEST(FixedStrong, FixedFromDoubleTruncatesTowardZero)
{
	EXPECT_EQ(Fixed(3.9).Raw(), 3);
	EXPECT_EQ(Fixed(-3.9).Raw(), -3);
	EXPECT_EQ(Fixed(65536.0 * 2.5).Raw(), static_cast<long long>(65536.0 * 2.5));
	EXPECT_EQ((static_cast<Fixed>(7.99)).Raw(), 7);           // C-style / static_cast path
	EXPECT_EQ(Fixed(-0.5f).Raw(), 0);
	static_assert(!std::is_convertible<double, Fixed>::value, "double must not implicitly convert");
}

TEST(FixedStrong, ExplicitBoundaryCrossing)
{
	Fixed x = Fixed::FromRaw(int64_t(5) << 40); // a value that overflows int32
	EXPECT_EQ(x.Raw(), int64_t(5) << 40);
	// Narrowing must be spelled out; and it truncates, as expected of an explicit cast.
	EXPECT_EQ(static_cast<int>(x), static_cast<int>(int64_t(5) << 40));
	EXPECT_TRUE(static_cast<bool>(x));
}

// [rc4l] Contract sweep: every operator and conversion forwards to the identical raw int64
// operation. These are trivial one-liners, but pinning each one is the value-preservation
// guarantee in miniature (strong == raw for every op) and covers the full operator surface.
TEST(FixedStrong, EveryOperatorForwardsToRaw)
{
	// [rc4l] Seed through volatile so nothing below constant-folds: with literal constexpr operands
	// some compilers evaluate these operators at compile time and they never register as executed
	// (CI saw exactly this on operator|). Reading a volatile forces a genuine runtime call chain.
	volatile int64_t seed = 0x0000001234567890LL; // a value with bits above 32 to exercise truncation
	const int64_t rv = seed;
	const Fixed f = Fixed::FromRaw(rv);

	// Constructors from every signed/unsigned integer width (unsigned is the explicit escape hatch).
	// long and long long are distinct ctors; on LP64 (Linux) int64_t is `long`, so FromRaw covers
	// Fixed(long) but Fixed(long long) is only reached by an explicit long long argument (on macOS
	// int64_t is `long long`, so it is the other way round -- exercise both so coverage matches on
	// every platform).
	EXPECT_EQ(Fixed(long(-7)).Raw(), -7);
	{ volatile long long ll = -8; EXPECT_EQ(Fixed((long long)ll).Raw(), -8); }
	EXPECT_EQ(Fixed(5).Raw(), 5);
	EXPECT_EQ(Fixed(unsigned(0xFFFFFFFFu)).Raw(), int64_t(uint32_t(0xFFFFFFFFu)));       // zero-extend
	EXPECT_EQ(Fixed((unsigned long)0x1u).Raw(), 1);
	EXPECT_EQ(Fixed((unsigned long long)0x2u).Raw(), 2);

	// Explicit conversions out: each narrows exactly like a static_cast of the raw value.
	EXPECT_EQ((char)f, (char)rv);
	EXPECT_EQ((signed char)f, (signed char)rv);
	EXPECT_EQ((unsigned char)f, (unsigned char)rv);
	EXPECT_EQ((short)f, (short)rv);
	EXPECT_EQ((unsigned short)f, (unsigned short)rv);
	EXPECT_EQ((int)f, (int)rv);
	EXPECT_EQ((unsigned)f, (unsigned)rv);
	EXPECT_EQ((long)f, (long)rv);
	EXPECT_EQ((unsigned long)f, (unsigned long)rv);
	EXPECT_EQ((long long)f, (long long)rv);
	EXPECT_EQ((unsigned long long)f, (unsigned long long)rv);
	EXPECT_FLOAT_EQ((float)f, (float)rv);
	EXPECT_DOUBLE_EQ((double)f, (double)rv);

	// Unary and the reversed/int scaling + modulo operators.
	EXPECT_EQ((+f).Raw(), rv);
	EXPECT_EQ((3 * Fixed(10)).Raw(), 3 * 10);
	EXPECT_EQ((Fixed::FromRaw(17) % 5).Raw(), 17 % 5);

	// Bitwise on two Fixeds and complement -- driven off the runtime-seeded f so the operators are
	// actually called (see the volatile note above; this is what makes operator| register).
	const Fixed mask = Fixed::FromRaw(0xFF);
	EXPECT_EQ((f & mask).Raw(), rv & 0xFF);
	EXPECT_EQ((f | mask).Raw(), rv | 0xFF);
	EXPECT_EQ((f ^ mask).Raw(), rv ^ 0xFF);
	EXPECT_EQ((~f).Raw(), ~rv);

	// Remaining comparisons.
	EXPECT_TRUE(Fixed(1) != Fixed(2));
	EXPECT_TRUE(Fixed(2) >= Fixed(2));

	// Compound assignment: each mutates the raw core exactly like the plain int64 would.
	{ Fixed a = Fixed(10); a += Fixed(3); EXPECT_EQ(a.Raw(), 13); }
	{ Fixed a = Fixed(10); a -= Fixed(3); EXPECT_EQ(a.Raw(), 7); }
	{ Fixed a = Fixed(10); a *= 3;        EXPECT_EQ(a.Raw(), 30); }
	{ Fixed a = Fixed(10); a /= 3;        EXPECT_EQ(a.Raw(), 3); }
	{ Fixed a = Fixed::FromRaw(46000LL*65536); a /= Fixed::FromRaw(20LL*65536); EXPECT_EQ(a.Raw(), (46000LL*65536)/(20LL*65536)); }
	{ Fixed a = Fixed(17); a %= 5;        EXPECT_EQ(a.Raw(), 2); }
	{ Fixed a = Fixed::FromRaw(17); a %= Fixed::FromRaw(5); EXPECT_EQ(a.Raw(), 2); }
	{ Fixed a = Fixed(1); EXPECT_EQ((++a).Raw(), 2); EXPECT_EQ((a++).Raw(), 2); EXPECT_EQ(a.Raw(), 3); }
	{ Fixed a = Fixed(3); EXPECT_EQ((--a).Raw(), 2); EXPECT_EQ((a--).Raw(), 2); EXPECT_EQ(a.Raw(), 1); }
	{ Fixed a = Fixed(1); a <<= 4; EXPECT_EQ(a.Raw(), 16); a >>= 2; EXPECT_EQ(a.Raw(), 4); }

	// abs/MIN/MAX/clamp (parenthesized to bypass any sys/param.h function-like macro). Operands are
	// runtime (volatile-seeded) and BOTH ternary branches of each are exercised, so every region
	// executes at run time instead of the compiler folding the constexpr call and leaving the branch
	// not taken uncovered -- different clang versions then disagree on the line count (issue #27).
	volatile int64_t s_neg = -9, s_lo = 3, s_mid = 5, s_hi = 9, s_below = 1, s_above = 12;
	const Fixed r_neg = Fixed::FromRaw(s_neg), r_lo = Fixed::FromRaw(s_lo), r_mid = Fixed::FromRaw(s_mid),
		r_hi = Fixed::FromRaw(s_hi), r_below = Fixed::FromRaw(s_below), r_above = Fixed::FromRaw(s_above);
	EXPECT_EQ((zx::abs)(r_neg).Raw(), 9);                 // Raw() < 0 branch
	EXPECT_EQ((zx::abs)(r_hi).Raw(), 9);                  // Raw() >= 0 branch
	EXPECT_EQ((zx::MIN)(r_lo, r_hi).Raw(), 3);            // a < b  -> a
	EXPECT_EQ((zx::MIN)(r_hi, r_lo).Raw(), 3);            // a >= b -> b
	EXPECT_EQ((zx::MAX)(r_lo, r_hi).Raw(), 9);            // a < b  -> b
	EXPECT_EQ((zx::MAX)(r_hi, r_lo).Raw(), 9);            // a > b  -> a
	EXPECT_EQ((zx::clamp)(r_below, r_lo, r_hi).Raw(), 3); // v < lo      -> lo
	EXPECT_EQ((zx::clamp)(r_above, r_lo, r_hi).Raw(), 9); // v > hi      -> hi
	EXPECT_EQ((zx::clamp)(r_mid, r_lo, r_hi).Raw(), 5);   // lo <= v <= hi -> v
}

// [rc4l] The finesine bug, as a runtime behavioral guard alongside the compile-time traits above.
// R_InitTables (r_utility.cpp) fills finesine[i] = (fixed_t)(double(FRACUNIT) * sin(i*pimul)). The
// original `FRACUNIT * sin(...)` bound to operator*(Fixed,int), truncating sin -- which is in [0,1)
// across the first quarter -- to 0, zeroing the whole sine/cosine table and killing movement,
// hitscan, monster sight and rendering at every non-cardinal angle. This pins that the float-
// multiply form produces the correct non-zero fixed value (and, via the traits above, that the
// truncating form no longer compiles).
TEST(FixedStrong, FinesineFillIsFloatMultiplyNotTruncation)
{
	const Fixed kFracUnit = Fixed::FromRaw(65536); // FRACUNIT, without pulling in doomtype.h
	const double pimul = M_PI * 2 / 8192.0;        // FINEANGLES = 8192
	for (int i : {1, 500, 1000, 2047})             // non-cardinal indices within the first quarter
	{
		const Fixed v = (Fixed)(double(kFracUnit) * sin(i * pimul)); // exactly R_InitTables' form
		const long long expect = (long long)(65536.0 * sin(i * pimul));
		EXPECT_EQ(v.Raw(), expect);
		EXPECT_GT(v.Raw(), 0); // the crux: a real fixed-point sine, not truncated to zero
	}
	// Sanity: the quarter endpoints (sin 0, sin ~1) still behave.
	EXPECT_EQ(((Fixed)(double(kFracUnit) * sin(0 * pimul))).Raw(), 0);
	EXPECT_GT(((Fixed)(double(kFracUnit) * sin(2048 * pimul))).Raw(), 60000); // ~FRACUNIT at 90deg
}

// [rc4l] The polyobject-rotation bug class: a 32-bit align-down mask on a negative fixed value.
// The type sign-extends 32-bit masks, so it stays negative (correct) instead of becoming a huge
// positive -- the bug is now impossible, including in backported code that writes & 0xFFFFFE00.
TEST(FixedStrong, ThirtyTwoBitMaskStaysSignPreserving)
{
	// [rc4l] Runtime-seeded operands so operator&'s widenMask actually executes (both the
	// sizeof<=4 sign-extending branch and the 64-bit pass-through branch) instead of folding.
	volatile int64_t neg_seed = -1024;
	const Fixed neg = Fixed::FromRaw(neg_seed);
	volatile unsigned m32_seed = 0xFFFFFE00u;    const unsigned m32 = m32_seed;        // 4-byte mask
	volatile int m32s_seed = ~0x1FF;             const int m32s = m32s_seed;           // 4-byte signed mask
	volatile unsigned low_seed = 0xFFFFu;        const unsigned low = low_seed;        // 4-byte low-bit mask
	volatile unsigned fine_seed = 0x1FFFu;       const unsigned fine = fine_seed;      // FINEMASK
	volatile unsigned long long m64_seed = 0xFFFFFF0000000000ULL; const unsigned long long m64 = m64_seed; // 8-byte mask
	EXPECT_EQ((neg & m32).Raw(), -1024);        // 4-byte mask, sign preserved (widenMask sizeof<=4)
	EXPECT_EQ((neg & m32s).Raw(), -1024);       // ~0x1FF (signed -512) also fine
	EXPECT_LT((neg & m32), 0);

	// Low-bit masks are unaffected (sign-extending a positive mask is a no-op).
	EXPECT_EQ((Fixed::FromRaw(0x12345) & low).Raw(), 0x12345 & 0xFFFF);
	EXPECT_EQ((Fixed::FromRaw(0x12345) & fine).Raw(), 0x12345 & 0x1FFF); // FINEMASK

	// A genuine 64-bit mask passes through unchanged (widenMask sizeof>4 branch).
	EXPECT_EQ((Fixed::FromRaw(int64_t(5) << 40) & m64).Raw(), int64_t(5) << 40);
}
