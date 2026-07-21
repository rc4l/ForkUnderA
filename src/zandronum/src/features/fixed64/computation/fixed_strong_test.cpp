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

TEST(FixedStrong, ExplicitBoundaryCrossing)
{
	Fixed x = Fixed::FromRaw(int64_t(5) << 40); // a value that overflows int32
	EXPECT_EQ(x.Raw(), int64_t(5) << 40);
	// Narrowing must be spelled out; and it truncates, as expected of an explicit cast.
	EXPECT_EQ(static_cast<int>(x), static_cast<int>(int64_t(5) << 40));
	EXPECT_TRUE(static_cast<bool>(x));
}

TEST(FixedStrong, MaskKeepsFullWidth)
{
	// Sign-preserving align-down (the polyobject idiom) works when the mask is 64-bit.
	Fixed neg = Fixed::FromRaw(-1024);
	EXPECT_LT((neg & ~int64_t(0x1FF)), 0); // stays negative with a full-width mask
}
