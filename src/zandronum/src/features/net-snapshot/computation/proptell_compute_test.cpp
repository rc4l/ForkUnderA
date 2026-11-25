// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: proptell_compute_test.cpp
//
// Description: Tests for proptell_compute: the mark over the bytes that travel
// and its reserved 0, then the cases the decision has to get right -- a set to
// tell, a first change partway through a life, a change after one was told, a
// return to the class defaults, a cleared mark and an id handed to a new
// entity.
//
//-----------------------------------------------------------------------------

#include "proptell_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

namespace
{

// A scale-only body as WritePropsBody lays it out: the mask, the length, the flags and two scales.
const unsigned char SCALE_12[] = { 0x40, 6, 0x01, 0x00, 0x01, 0x00, 0x01 };
const unsigned char SCALE_13[] = { 0x40, 6, 0x01, 0x40, 0x01, 0x40, 0x01 };
// A body that names nothing, which is what a return to the class defaults sends.
const unsigned char NOTHING[] = { 0x40, 1, 0x00 };

unsigned int Mark(const unsigned char* body, unsigned int len)
{
	return ComputePropMark(body, len);
}

} // namespace

TEST( ComputePropMark, TheSameBytesMarkTheSameAndDifferentBytesDoNot )
{
	EXPECT_EQ( Mark( SCALE_12, sizeof( SCALE_12 ) ), Mark( SCALE_12, sizeof( SCALE_12 ) ) );
	EXPECT_NE( Mark( SCALE_12, sizeof( SCALE_12 ) ), Mark( SCALE_13, sizeof( SCALE_13 ) ) );
	EXPECT_NE( Mark( SCALE_12, sizeof( SCALE_12 ) ), Mark( NOTHING, sizeof( NOTHING ) ) );
	// A shorter run of the same bytes is its own value, so a truncated name cannot read as a
	// whole one.
	EXPECT_NE( Mark( SCALE_12, sizeof( SCALE_12 ) ), Mark( SCALE_12, sizeof( SCALE_12 ) - 1 ) );
}

TEST( ComputePropMark, NothingMarksAsNone )
{
	// Empty, and a body whose FNV-1a lands exactly on 0: both have to read as a real mark, or an
	// id would look as though nothing had been told for it.
	EXPECT_NE( SNAPPROP_MARK_NONE, Mark( SCALE_12, 0 ) );
	EXPECT_NE( SNAPPROP_MARK_NONE, Mark( nullptr, 4 ) );
	const unsigned char hashesToZero[] = { 204, 36, 49, 196 };
	EXPECT_EQ( 1u, Mark( hashesToZero, sizeof( hashesToZero ) ) );
}

TEST( ComputePropTellOrSlot, ASetWithNothingToldYetIsToldOnce )
{
	const unsigned int m = Mark( SCALE_12, sizeof( SCALE_12 ) );
	EXPECT_EQ( SNAPPROPACT_TELL, ComputePropTellOrSlot( SNAPPROP_MARK_NONE, true, true, m ) );
}

TEST( ComputePropTellOrSlot, AFirstChangePartwayThroughALifeIsAlsoJustTold )
{
	// HRCometSpark's shape: its class scale all its life, then one change when it dies.
	// Nothing was told for it, so it is told once a client and needs no slot.
	const unsigned int now = Mark( SCALE_13, sizeof( SCALE_13 ) );
	EXPECT_EQ( SNAPPROPACT_TELL, ComputePropTellOrSlot( SNAPPROP_MARK_NONE, true, true, now ) );
	// An entity still at its class defaults asks for nothing, whenever it is seen.
	EXPECT_EQ( SNAPPROPACT_NOTHING,
		ComputePropTellOrSlot( SNAPPROP_MARK_NONE, false, false, now ) );
}

TEST( ComputePropTellOrSlot, AnEntityThatIsItsClassAsksForNothing )
{
	const unsigned int m = Mark( NOTHING, sizeof( NOTHING ) );
	EXPECT_EQ( SNAPPROPACT_NOTHING, ComputePropTellOrSlot( SNAPPROP_MARK_NONE, false, false, m ) );
	// Off its defaults by a field no record carries -- a material slot with no rows set -- is
	// still nothing to send.
	EXPECT_EQ( SNAPPROPACT_NOTHING, ComputePropTellOrSlot( SNAPPROP_MARK_NONE, true, false, m ) );
}

TEST( ComputePropTellOrSlot, AChangeAfterASetWasToldTakesASlot )
{
	const unsigned int told = Mark( SCALE_12, sizeof( SCALE_12 ) );
	const unsigned int now = Mark( SCALE_13, sizeof( SCALE_13 ) );
	EXPECT_EQ( SNAPPROPACT_SLOT, ComputePropTellOrSlot( told, true, true, now ) );
	// Unchanged, which is every pump of a set-once entity's life, asks for nothing.
	EXPECT_EQ( SNAPPROPACT_NOTHING, ComputePropTellOrSlot( told, true, true, told ) );
}

TEST( ComputePropTellOrSlot, AReturnToTheClassDefaultsTakesASlotToSaySo )
{
	const unsigned int told = Mark( SCALE_12, sizeof( SCALE_12 ) );
	const unsigned int back = Mark( NOTHING, sizeof( NOTHING ) );
	// offDefaults is false and the body names nothing, and it still has to travel.
	EXPECT_EQ( SNAPPROPACT_SLOT, ComputePropTellOrSlot( told, false, false, back ) );
}

TEST( ComputePropTellOrSlot, AClearedMarkIsToldAgain )
{
	// The mark is cleared when an id's entity leaves, so the next entity on that id is told its
	// own set rather than read as a change.
	const unsigned int m = Mark( SCALE_13, sizeof( SCALE_13 ) );
	EXPECT_EQ( SNAPPROPACT_TELL, ComputePropTellOrSlot( SNAPPROP_MARK_NONE, true, true, m ) );
}

TEST( ComputePropTellOrSlot, AnIdHandedToANewEntityWithinOnePumpTakesASlot )
{
	// No leave was ever seen, so the mark is the previous entity's: the new one's value does not
	// match it, and the difference travels.
	const unsigned int previous = Mark( SCALE_12, sizeof( SCALE_12 ) );
	const unsigned int mine = Mark( SCALE_13, sizeof( SCALE_13 ) );
	EXPECT_EQ( SNAPPROPACT_SLOT, ComputePropTellOrSlot( previous, true, true, mine ) );
	// And where the new entity's value happens to match, there is nothing to tell a client.
	EXPECT_EQ( SNAPPROPACT_NOTHING, ComputePropTellOrSlot( previous, true, true, previous ) );
}
