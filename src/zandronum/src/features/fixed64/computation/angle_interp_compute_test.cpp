// [rc4l] Regression test for the "+right spins far faster than +left" bug.
//
// The view-angle interpolation lerps a wrapping BAM angle by frac. A right turn produces a
// negative per-tic delta, which as an unsigned 32-bit difference is a huge value; feeding it
// straight into the widened (64-bit) fixed multiply zero-extends it and the view overshoots by
// nearly half a circle -- but only rightward, since left turns give a small positive delta.
// InterpolateAngleBAM reinterprets the difference as int32 first, restoring symmetry.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gtest/gtest.h"

#include <cstdint>

#include "features/fixed64/computation/angle_interp_compute.h"
#include "features/fixed64/computation/fixed64_scale_compute.h"

namespace
{
// One tic of a stock 640/tic keyboard turn, in BAM: 640 << 16.
constexpr int32_t kTicStep = 640 << 16;   // 41943040
constexpr int64_t kHalf = 32768;          // frac = 0.5 in 16.16
} // namespace

// [rc4l] The core property: turning right and left by equal amounts must interpolate to equal
// magnitudes in opposite directions. This is what the bug broke.
TEST(AngleInterp, RightAndLeftAreSymmetric)
{
	// Left: angle increases by one tic. Right: angle decreases by one tic (wraps below zero).
	const uint32_t left = zx::InterpolateAngleBAM(0, uint32_t(kTicStep), kHalf);
	const uint32_t right = zx::InterpolateAngleBAM(0, uint32_t(0u - uint32_t(kTicStep)), kHalf);

	EXPECT_EQ(left, uint32_t(kTicStep / 2));               // +half a tic
	EXPECT_EQ(int32_t(right), -(kTicStep / 2));            // -half a tic (equal magnitude)
	EXPECT_EQ(left, uint32_t(-(int32_t)right));            // |left| == |right|
}

// [rc4l] A right turn must interpolate to a small step, not a half-circle jump. Compares the
// fixed path against the pre-fix path (unsigned delta straight into the 64-bit multiply).
TEST(AngleInterp, RightTurnDoesNotOvershoot)
{
	const uint32_t rawUnsignedDelta = 0u - uint32_t(kTicStep); // ~0xFD800000, what a right turn yields

	// Pre-fix behaviour: the huge unsigned delta zero-extends to a large positive multiply.
	const uint32_t broken = uint32_t(zx::Fixed64Mul(kHalf, int64_t(rawUnsignedDelta)));
	// Fixed behaviour: signed reinterpret first.
	const uint32_t fixed = zx::InterpolateAngleBAM(0, rawUnsignedDelta, kHalf);

	EXPECT_NE(broken, fixed);
	EXPECT_GT(broken, uint32_t(0x40000000));       // broken jumps more than a quarter circle
	EXPECT_LT(int32_t(fixed), 0);                  // fixed is a small negative (rightward) step
	EXPECT_GT(int32_t(fixed), -kTicStep);          // and smaller in magnitude than a full tic
}

// [rc4l] Endpoints: frac 0 stays at the old angle, frac 1 (FRACUNIT) reaches the new angle,
// for both directions.
TEST(AngleInterp, EndpointsExact)
{
	const uint32_t rightNew = 0u - uint32_t(kTicStep);
	EXPECT_EQ(zx::InterpolateAngleBAM(1000, 1000 + uint32_t(kTicStep), 0), uint32_t(1000));
	EXPECT_EQ(zx::InterpolateAngleBAM(1000, 1000 + uint32_t(kTicStep), 65536), uint32_t(1000 + kTicStep));
	EXPECT_EQ(zx::InterpolateAngleBAM(0, rightNew, 0), uint32_t(0));
	EXPECT_EQ(zx::InterpolateAngleBAM(0, rightNew, 65536), rightNew);
}
