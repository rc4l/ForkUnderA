// [rc4l] Regression guard for the savegame side of the fixed_t widening.
//
// fixed_t has no dedicated FArchive overload, so `arc << <fixed_t>` serializes at the type's
// native width. Once fixed_t is wider than 32 bits, every serialized fixed_t field (actor x/y/z,
// scale, floorz, plane coefficients, ...) doubled on disk, silently changing the level-snapshot
// format. The version constants must then reject every 32-bit-era snapshot, or old saves load
// misaligned and corrupt actor state. This mirrors the static_assert in p_saveg.cpp so the
// invariant is also exercised by the test suite.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gtest/gtest.h"

#include "basictypes.h"
#include "version.h"

// [rc4l] The core invariant: if fixed_t is not 32-bit, the save format must have moved past the
// last 32-bit format version -- both the writer tag (SAVEVER) and the minimum accepted version
// (MINSAVEVER), so pre-widening saves are refused rather than misread.
TEST(SaveFormat, VersionMovedPastLast32BitFormatWhenFixedIsWide)
{
	if (sizeof(fixed_t) != 4)
	{
		EXPECT_GT(SAVEVER, LAST_FIXED32_SAVEVER)
			<< "fixed_t widened but SAVEVER still tags snapshots as the old 32-bit format";
		EXPECT_GT(MINSAVEVER, LAST_FIXED32_SAVEVER)
			<< "fixed_t widened but MINSAVEVER still accepts 32-bit-format snapshots";
	}
}

// [rc4l] A build must always be able to load the saves it writes.
TEST(SaveFormat, WriterVersionIsWithinAcceptedWindow)
{
	EXPECT_GE(SAVEVER, MINSAVEVER);
}

// [rc4l] Documents the current state: the widening is in effect (fixed_t == 8 bytes), so the
// guard above is live rather than vacuous. If fixed_t is ever narrowed back, update this.
TEST(SaveFormat, FixedIsSixtyFourBit)
{
	EXPECT_EQ(sizeof(fixed_t), static_cast<size_t>(8));
}
