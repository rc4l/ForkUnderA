// [rc4l] Verifies the fixed-point math layer is strict-clean: under ZX_STRONG_FIXED the scale
// family accepts and returns zx::Fixed, while the plain int64 overloads still serve integer math.
// This is the first bottom-of-graph milestone of the strong-fixed migration.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#define ZX_STRONG_FIXED 1

#include "gtest/gtest.h"

#include <type_traits>

#include "features/fixed64/computation/fixed_strong.h"
#include "computation/fixedmath.h"

using zx::Fixed;

TEST(FixedMathStrong, ScaleFamilyReturnsFixed)
{
	const Fixed a = 3 * 65536; // 3.0
	const Fixed b = 2 * 65536; // 2.0

	static_assert(std::is_same<decltype(MulScale16(a, b)), Fixed>::value,
		"MulScale16(Fixed,Fixed) must return Fixed");

	EXPECT_EQ(MulScale16(a, b).Raw(), zx::MulScale64(a.Raw(), b.Raw(), 16)); // 3*2 = 6.0
	EXPECT_EQ(DivScale16(Fixed(6 * 65536), b).Raw(), zx::DivScale64(6 * 65536, 16, b.Raw()));
	EXPECT_EQ(DMulScale16(a, b, a, b).Raw(), zx::DMulScale64(a.Raw(), b.Raw(), a.Raw(), b.Raw(), 16));
}

// [rc4l] The int64 overloads still resolve for genuine integer (angle/count) scaling.
TEST(FixedMathStrong, IntegerScaleStillWorks)
{
	EXPECT_EQ(MulScale16(static_cast<int64_t>(3), static_cast<int64_t>(4)), zx::MulScale64(3, 4, 16));
	EXPECT_EQ(Scale(static_cast<int64_t>(100), static_cast<int64_t>(7), static_cast<int64_t>(2)),
		zx::ComputeMulDivS64(100, 7, 2));
}
