// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// The point of this suite is the fixed_t widening: every case that matters feeds a coordinate whose
// raw fixed value exceeds INT32_MAX, which the pre-widening 32-bit conversion path would corrupt.
#include "features/hwrender/computation/vertexconvert_compute.h"

#include <cstdint>
#include <gtest/gtest.h>

namespace
{

constexpr int64_t kFracUnit = 65536; // 1.0 map unit in raw fixed

TEST(VertexConvert, UnitAndZeroAndFraction)
{
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(kFracUnit), 1.0);
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(-kFracUnit), -1.0);
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(0), 0.0);
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(kFracUnit / 2), 0.5);
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(-(kFracUnit / 4)), -0.25);
}

// [rc4l] The regression the whole port must not reintroduce. 46341 units is the smallest whole-unit
// coordinate whose raw fixed value exceeds INT32_MAX (46341 * 65536 = 3,037,691,904 > 2,147,483,647),
// so `(float)(int32_t)raw` would wrap to a negative/garbage value. The 64-bit path must be exact.
TEST(VertexConvert, SurvivesPastThe32BitBoundary)
{
	const int64_t raw = static_cast<int64_t>(46341) * kFracUnit;
	ASSERT_GT(raw, static_cast<int64_t>(INT32_MAX));
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(raw), 46341.0);

	// Prove the naive 32-bit truncation really would have been wrong, so this test is guarding a
	// real hazard and not a tautology.
	const double naive = static_cast<double>(static_cast<int32_t>(raw)) * (1.0 / 65536.0);
	EXPECT_NE(naive, 46341.0);
}

TEST(VertexConvert, LargeNegativeAndFarSpan)
{
	// Two points ~46k units apart -- the dist_compute failure signature, in vertex form.
	const int64_t a = static_cast<int64_t>(40000) * kFracUnit;
	const int64_t b = static_cast<int64_t>(-40000) * kFracUnit;
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(a), 40000.0);
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(b), -40000.0);
	EXPECT_DOUBLE_EQ(zx::FixedRawToUnits(a) - zx::FixedRawToUnits(b), 80000.0);
}

TEST(VertexConvert, FloatVariantTracksDoubleForLargeCoords)
{
	const int64_t raw = static_cast<int64_t>(50000) * kFracUnit;
	// The integer part of a 50000-unit coordinate is exactly representable in float, so the narrowed
	// result must still land on 50000, not a wrapped 32-bit value.
	EXPECT_FLOAT_EQ(zx::FixedRawToUnitsF(raw), 50000.0f);
	EXPECT_NEAR(static_cast<double>(zx::FixedRawToUnitsF(raw)), zx::FixedRawToUnits(raw), 0.5);
}

// [rc4l] The Doom->GL axis swap: (x, y, z)_doom -> (x, z, y)_gl, with every coordinate large enough
// to catch a truncation in any of the three lanes.
TEST(VertexConvert, VertexAxisSwapAndConversion)
{
	const int64_t x = static_cast<int64_t>(46341) * kFracUnit;
	const int64_t y = static_cast<int64_t>(-50000) * kFracUnit;
	const int64_t z = static_cast<int64_t>(60000) * kFracUnit;

	const zx::RenderVertexF v = zx::FixedVertexToRender(x, y, z);
	EXPECT_FLOAT_EQ(v.x, 46341.0f); // doom x
	EXPECT_FLOAT_EQ(v.y, 60000.0f); // doom z -> gl y
	EXPECT_FLOAT_EQ(v.z, -50000.0f); // doom y -> gl z
}

} // namespace
