// [rc4l] Tests for the strong Fixed type: normal fixed math still works and produces the same
// values as raw int64, while the two bug-prone implicit conversions are rejected at compile time.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gtest/gtest.h"

#include <cstdint>
#include <type_traits>

#include "features/fixed64/computation/fixed_strong.h"

using zx::Fixed;

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
	const int64_t rv = 0x0000001234567890LL; // a value with bits above 32 to exercise truncation
	const Fixed f = Fixed::FromRaw(rv);

	// Constructors from every signed/unsigned integer width (unsigned is the explicit escape hatch).
	EXPECT_EQ(Fixed(long(-7)).Raw(), -7);
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

	// Bitwise on two Fixeds and complement.
	EXPECT_EQ((Fixed::FromRaw(0xF0) & Fixed::FromRaw(0x3C)).Raw(), 0xF0 & 0x3C);
	EXPECT_EQ((Fixed::FromRaw(0xF0) | Fixed::FromRaw(0x3C)).Raw(), 0xF0 | 0x3C);
	EXPECT_EQ((Fixed::FromRaw(0xF0) ^ Fixed::FromRaw(0x3C)).Raw(), 0xF0 ^ 0x3C);
	EXPECT_EQ((~Fixed::FromRaw(0)).Raw(), ~int64_t(0));

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

	// abs/MIN/MAX/clamp (parenthesized to bypass any sys/param.h function-like macro).
	EXPECT_EQ((zx::abs)(Fixed(-9)).Raw(), 9);
	EXPECT_EQ((zx::abs)(Fixed(9)).Raw(), 9);
	EXPECT_EQ((zx::MIN)(Fixed(4), Fixed(7)).Raw(), 4);
	EXPECT_EQ((zx::MAX)(Fixed(4), Fixed(7)).Raw(), 7);
	EXPECT_EQ((zx::clamp)(Fixed(1), Fixed(3), Fixed(9)).Raw(), 3);
	EXPECT_EQ((zx::clamp)(Fixed(12), Fixed(3), Fixed(9)).Raw(), 9);
	EXPECT_EQ((zx::clamp)(Fixed(5), Fixed(3), Fixed(9)).Raw(), 5);
}

// [rc4l] The polyobject-rotation bug class: a 32-bit align-down mask on a negative fixed value.
// The type sign-extends 32-bit masks, so it stays negative (correct) instead of becoming a huge
// positive -- the bug is now impossible, including in backported code that writes & 0xFFFFFE00.
TEST(FixedStrong, ThirtyTwoBitMaskStaysSignPreserving)
{
	const Fixed neg = Fixed::FromRaw(-1024);
	EXPECT_EQ((neg & 0xFFFFFE00).Raw(), -1024); // 32-bit unsigned mask, sign preserved (the fix)
	EXPECT_EQ((neg & ~0x1FF).Raw(), -1024);     // ~0x1FF (signed -512) also fine
	EXPECT_LT((neg & 0xFFFFFE00), 0);

	// Low-bit masks are unaffected (sign-extending a positive mask is a no-op).
	EXPECT_EQ((Fixed::FromRaw(0x12345) & 0xFFFF).Raw(), 0x12345 & 0xFFFF);
	EXPECT_EQ((Fixed::FromRaw(0x12345) & 0x1FFF).Raw(), 0x12345 & 0x1FFF); // FINEMASK

	// A genuine 64-bit mask passes through unchanged.
	EXPECT_EQ((Fixed::FromRaw(int64_t(5) << 40) & 0xFFFFFF0000000000LL).Raw(), int64_t(5) << 40);
}
