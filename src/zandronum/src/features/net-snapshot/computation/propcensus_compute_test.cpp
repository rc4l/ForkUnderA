// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: propcensus_compute_test.cpp
//
// Description: Tests for propcensus_compute: the tally by class, the order
// (most slots first, the lower class id breaking a tie), the per-bit counts,
// an unnamed class, and what happens with no room and no input.
//
//-----------------------------------------------------------------------------

#include "propcensus_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

namespace
{

SnapPropSlotSeen Slot( unsigned short classId, unsigned char flags )
{
	SnapPropSlotSeen s;
	s.classId = classId;
	s.flags = flags;
	return s;
}

} // namespace

TEST( ComputePropClassCensus, NoSlotsNameNoClass )
{
	SnapPropClassCount out[4];
	unsigned int n = 99;
	ComputePropClassCensus( nullptr, 0, out, 4, n );
	EXPECT_EQ( 0u, n );
	const SnapPropSlotSeen seen[] = { Slot( 7, 1 ) };
	n = 99;
	ComputePropClassCensus( seen, 0, out, 4, n );
	EXPECT_EQ( 0u, n );
	n = 99;
	ComputePropClassCensus( seen, 1, nullptr, 4, n );
	EXPECT_EQ( 0u, n );
	n = 99;
	ComputePropClassCensus( seen, 1, out, 0, n );
	EXPECT_EQ( 0u, n );
}

TEST( ComputePropClassCensus, OneClassCountsItsSlotsAndItsBits )
{
	// Bits 0 and 3 on one slot, bit 0 alone on the other, and bit 4 above the four counted.
	const SnapPropSlotSeen seen[] = { Slot( 12, 0x09 ), Slot( 12, 0x01 ), Slot( 12, 0x10 ) };
	SnapPropClassCount out[4];
	unsigned int n = 0;
	ComputePropClassCensus( seen, 3, out, 4, n );
	ASSERT_EQ( 1u, n );
	EXPECT_EQ( 12, out[0].classId );
	EXPECT_EQ( 3u, out[0].slots );
	EXPECT_EQ( 2u, out[0].byBit[0] );
	EXPECT_EQ( 0u, out[0].byBit[1] );
	EXPECT_EQ( 0u, out[0].byBit[2] );
	EXPECT_EQ( 1u, out[0].byBit[3] );
}

TEST( ComputePropClassCensus, TheClassWithTheMostSlotsComesFirst )
{
	const SnapPropSlotSeen seen[] = { Slot( 5, 1 ), Slot( 9, 1 ), Slot( 9, 2 ), Slot( 9, 4 ),
		Slot( 3, 1 ), Slot( 3, 1 ) };
	SnapPropClassCount out[6];
	unsigned int n = 0;
	ComputePropClassCensus( seen, 6, out, 6, n );
	ASSERT_EQ( 3u, n );
	EXPECT_EQ( 9, out[0].classId );
	EXPECT_EQ( 3u, out[0].slots );
	EXPECT_EQ( 3, out[1].classId );
	EXPECT_EQ( 2u, out[1].slots );
	EXPECT_EQ( 5, out[2].classId );
	EXPECT_EQ( 1u, out[2].slots );
	EXPECT_EQ( 1u, out[0].byBit[0] );
	EXPECT_EQ( 1u, out[0].byBit[1] );
	EXPECT_EQ( 1u, out[0].byBit[2] );
}

TEST( ComputePropClassCensus, EqualCountsGoByClassIdAndAnUnnamedClassIsOneOfThem )
{
	// Class 0 is a slot whose entity could not be named; it counts as its own class, and with
	// the same count it sorts ahead of the named ones.
	const SnapPropSlotSeen seen[] = { Slot( 40, 1 ), Slot( 8, 1 ), Slot( 0, 1 ) };
	SnapPropClassCount out[3];
	unsigned int n = 0;
	ComputePropClassCensus( seen, 3, out, 3, n );
	ASSERT_EQ( 3u, n );
	EXPECT_EQ( 0, out[0].classId );
	EXPECT_EQ( 8, out[1].classId );
	EXPECT_EQ( 40, out[2].classId );
	EXPECT_EQ( 1u, out[0].slots );
	EXPECT_EQ( 1u, out[1].slots );
	EXPECT_EQ( 1u, out[2].slots );
}

TEST( ComputePropClassCensus, AClassThatDoesNotFitIsDroppedAndTheRestStillCount )
{
	// Room for two classes: the third class seen is dropped, slots and all.
	const SnapPropSlotSeen seen[] = { Slot( 1, 1 ), Slot( 2, 1 ), Slot( 3, 1 ), Slot( 2, 2 ) };
	SnapPropClassCount out[2];
	unsigned int n = 0;
	ComputePropClassCensus( seen, 4, out, 2, n );
	ASSERT_EQ( 2u, n );
	EXPECT_EQ( 2, out[0].classId );
	EXPECT_EQ( 2u, out[0].slots );
	EXPECT_EQ( 1u, out[0].byBit[0] );
	EXPECT_EQ( 1u, out[0].byBit[1] );
	EXPECT_EQ( 1, out[1].classId );
	EXPECT_EQ( 1u, out[1].slots );
}
