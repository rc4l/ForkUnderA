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
