// [rc4l] Regression tests for the automap parchment scroll wrap.
//
// The bug: with fixed_t widened to 64-bit, AM_ScrollParchment's subtract-until-in-range loops
// were handed an offset ~9.2e18 out of range on the first tic after the automap opens (the
// FIXED_MAX sentinel in f_oldloc), so they ran ~10^13 iterations and hung the engine. Every test
// here also pins the iteration count the old loops would have needed, so the "this is a hang, not
// a slow path" claim is checked and not just asserted in a comment.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include <cstdint>
#include <limits>

#include "features/fixed64/computation/parchment_wrap_compute.h"

namespace
{
constexpr int MAPBITS = 12;

// One tile of an 87x88 AUTOPAGE (the lump that reproduced this), in map units.
constexpr int64_t kTileW = static_cast<int64_t>(87) << MAPBITS;
constexpr int64_t kTileH = static_cast<int64_t>(88) << MAPBITS;

// What the pre-fix loops did, as a trip count rather than by actually running them.
int64_t OldLoopIterations(int64_t offset, int64_t period)
{
	if (offset > 0)
		return (offset + period - 1) / period;
	if (offset <= -period)
		return -offset / period;
	return 0;
}
} // namespace

// [rc4l] The actual hang. AM_doFollowPlayer's first delta is taken against f_oldloc.x ==
// FIXED_MAX, which at 64-bit width drives the offset ~10^18 out of range. The wrap must still be
// O(1) and land in (-period, 0].
TEST(ParchmentWrap, FixedMaxSentinelOffsetWrapsInOneStep)
{
	const int64_t offset = std::numeric_limits<int64_t>::max() >> 4; // (x - FIXED_MAX) >> FRACTOMAPBITS

	const int64_t wrapped = zx::WrapParchmentOffset(offset, kTileW);
	EXPECT_GT(wrapped, -kTileW);
	EXPECT_LE(wrapped, 0);

	// Pre-fix: this many `mapxstart -= pwidth` steps, i.e. the engine never came back.
	EXPECT_GT(OldLoopIterations(offset, kTileW), int64_t(1) << 40);
}

// [rc4l] Same on the negative side -- scrolling the other way hits the `+= pwidth` loop.
TEST(ParchmentWrap, FixedMinSentinelOffsetWrapsInOneStep)
{
	const int64_t offset = std::numeric_limits<int64_t>::min() >> 4;

	const int64_t wrapped = zx::WrapParchmentOffset(offset, kTileH);
	EXPECT_GT(wrapped, -kTileH);
	EXPECT_LE(wrapped, 0);
	EXPECT_GT(OldLoopIterations(offset, kTileH), int64_t(1) << 40);
}

// [rc4l] Ordinary scrolling is unchanged: for offsets the old loops handled, the modulo returns
// exactly what subtract-until-in-range returned.
TEST(ParchmentWrap, MatchesOldLoopsInNormalRange)
{
	const int64_t period = kTileW;
	for (int64_t offset = -3 * period; offset <= 3 * period; offset += period / 7)
	{
		int64_t expected = offset;
		while (expected > 0)
			expected -= period;
		while (expected <= -period)
			expected += period;

		EXPECT_EQ(zx::WrapParchmentOffset(offset, period), expected) << "offset " << offset;
	}
}

// [rc4l] Range invariant holds for every sign/magnitude combination, including the exact
// boundaries the half-open range is fussy about (0 stays 0, -period wraps to 0).
TEST(ParchmentWrap, AlwaysLandsInHalfOpenRange)
{
	const int64_t period = kTileH;
	const int64_t offsets[] = {
		0, 1, -1, period, -period, period - 1, -period + 1, period + 1, -period - 1,
		1234567, -1234567, std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min() + 1,
	};

	for (int64_t offset : offsets)
	{
		const int64_t wrapped = zx::WrapParchmentOffset(offset, period);
		EXPECT_GT(wrapped, -period) << "offset " << offset;
		EXPECT_LE(wrapped, 0) << "offset " << offset;
		// The wrap only ever moves the offset by whole tiles, so the tiling never shifts.
		// Compared through `offset % period` rather than `offset - wrapped`: the latter overflows
		// for offsets near INT64_MAX, which is exactly the range this test cares about.
		EXPECT_EQ((offset % period - wrapped) % period, 0) << "offset " << offset;
	}

	EXPECT_EQ(zx::WrapParchmentOffset(0, period), 0);
	EXPECT_EQ(zx::WrapParchmentOffset(-period, period), 0);
}

// [rc4l] A zero/negative period would be a divide-by-zero, so it is refused rather than computed.
// (The old loops spun forever on it; nothing draws in either case.)
TEST(ParchmentWrap, NonPositivePeriodReturnsOffsetUnchanged)
{
	EXPECT_EQ(zx::WrapParchmentOffset(12345, 0), 12345);
	EXPECT_EQ(zx::WrapParchmentOffset(-12345, 0), -12345);
	EXPECT_EQ(zx::WrapParchmentOffset(12345, -4096), 12345);
}
