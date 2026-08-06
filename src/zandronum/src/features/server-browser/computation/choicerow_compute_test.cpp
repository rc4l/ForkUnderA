// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/choicerow_compute.h"

using zx::ChoiceCell;
using zx::ChoiceCellAt;
using zx::ChoiceHitTest;
using zx::ChoiceNormalise;
using zx::ChoiceStep;

// ---------------------------------------------------------------- layout

TEST( ChoiceRow, DividesTheRowEvenly )
{
	// [rc4l] Equal, not sized to their labels. A row where one option is three times the width of
	// the other reads as one being the important one, and the whole point is that they are
	// alternatives of equal standing.
	const ChoiceCell first = ChoiceCellAt( 0, 2, 100, 200, 0 );
	const ChoiceCell second = ChoiceCellAt( 1, 2, 100, 200, 0 );

	ASSERT_TRUE( first.valid );
	ASSERT_TRUE( second.valid );

	EXPECT_EQ( 100, first.x );
	EXPECT_EQ( 100, first.width );
	EXPECT_EQ( 200, second.x );
	EXPECT_EQ( 100, second.width );
}

TEST( ChoiceRow, LeavesTheGapBetweenCells )
{
	const ChoiceCell first = ChoiceCellAt( 0, 2, 0, 100, 10 );
	const ChoiceCell second = ChoiceCellAt( 1, 2, 0, 100, 10 );

	EXPECT_EQ( 0, first.x );
	EXPECT_EQ( 45, first.width );
	EXPECT_EQ( 55, second.x );
}

TEST( ChoiceRow, EndsExactlyWhereItWasTold )
{
	// A remainder from an uneven division goes to the last cell. A row ending a pixel short is
	// visible against a panel edge.
	for ( int total = 97; total <= 103; ++total )
	{
		const ChoiceCell last = ChoiceCellAt( 2, 3, 0, total, 4 );
		ASSERT_TRUE( last.valid ) << total;
		EXPECT_EQ( total, last.x + last.width ) << total;
	}
}

TEST( ChoiceRow, HandlesThreeAndFourAsHappilyAsTwo )
{
	// Two options today; a game mode picker is three or four, and only the array length changes.
	for ( int count = 1; count <= 5; ++count )
	{
		int previousEnd = -1;

		for ( int i = 0; i < count; ++i )
		{
			const ChoiceCell cell = ChoiceCellAt( i, count, 20, 300, 6 );
			ASSERT_TRUE( cell.valid ) << count << "," << i;
			EXPECT_GT( cell.width, 0 ) << count << "," << i;
			EXPECT_GE( cell.x, previousEnd ) << count << "," << i;
			previousEnd = cell.x + cell.width;
		}
	}
}

TEST( ChoiceRow, RefusesToPlaceWhatCannotBePlaced )
{
	EXPECT_FALSE( ChoiceCellAt( -1, 2, 0, 100, 0 ).valid );
	EXPECT_FALSE( ChoiceCellAt( 2, 2, 0, 100, 0 ).valid );
	EXPECT_FALSE( ChoiceCellAt( 0, 0, 0, 100, 0 ).valid );

	// More gap than room, and a row too narrow to divide at all.
	EXPECT_FALSE( ChoiceCellAt( 0, 2, 0, 10, 40 ).valid );
	EXPECT_FALSE( ChoiceCellAt( 0, 4, 0, 3, 0 ).valid );
	EXPECT_FALSE( ChoiceCellAt( 0, 2, 0, 0, 0 ).valid );
}

TEST( ChoiceRow, ANegativeGapIsTreatedAsNone )
{
	const ChoiceCell cell = ChoiceCellAt( 0, 2, 0, 100, -8 );

	ASSERT_TRUE( cell.valid );
	EXPECT_EQ( 50, cell.width );
}

// ---------------------------------------------------------------- pointing at it

TEST( ChoiceRowHit, FindsTheOptionUnderThePointer )
{
	EXPECT_EQ( 0, ChoiceHitTest( 10, 2, 0, 100, 10 ));
	EXPECT_EQ( 1, ChoiceHitTest( 90, 2, 0, 100, 10 ));
}

TEST( ChoiceRowHit, TheGapBelongsToNobody )
{
	// [rc4l] A click that lands between two answers is not evidence for either. Giving the gap to
	// the nearer one means a pointer a pixel off changes a setting the player was not aiming at.
	EXPECT_EQ( -1, ChoiceHitTest( 50, 2, 0, 100, 10 ));
}

TEST( ChoiceRowHit, MissesOutsideTheRowEntirely )
{
	EXPECT_EQ( -1, ChoiceHitTest( -5, 2, 0, 100, 10 ));
	EXPECT_EQ( -1, ChoiceHitTest( 500, 2, 0, 100, 10 ));
	EXPECT_EQ( -1, ChoiceHitTest( 10, 0, 0, 100, 10 ));
}

TEST( ChoiceRowHit, EveryCellIsHittableAtItsOwnEdges )
{
	// Swept because an off-by-one here is a control with a dead stripe down it that nobody notices
	// until they click the wrong half.
	for ( int i = 0; i < 3; ++i )
	{
		const ChoiceCell cell = ChoiceCellAt( i, 3, 7, 200, 5 );
		ASSERT_TRUE( cell.valid );

		EXPECT_EQ( i, ChoiceHitTest( cell.x, 3, 7, 200, 5 )) << i;
		EXPECT_EQ( i, ChoiceHitTest( cell.x + cell.width - 1, 3, 7, 200, 5 )) << i;
	}
}

// ---------------------------------------------------------------- moving between them

TEST( ChoiceRowStep, MovesOneAtATime )
{
	EXPECT_EQ( 1, ChoiceStep( 0, 3, 1 ));
	EXPECT_EQ( 0, ChoiceStep( 1, 3, -1 ));
}

TEST( ChoiceRowStep, StopsAtTheEndsRatherThanWrapping )
{
	// [rc4l] A ring is right for tabs, where the row is the whole world. It is wrong inside a form:
	// the arrows that reach this row also leave it, and a selection that wrapped would change the
	// answer while the player was trying to move past it.
	EXPECT_EQ( 0, ChoiceStep( 0, 3, -1 ));
	EXPECT_EQ( 2, ChoiceStep( 2, 3, 1 ));
}

TEST( ChoiceRowStep, AlwaysLandsSomewhereRealToIndexWith )
{
	// Swept, because the result goes straight into an array subscript.
	for ( int count = 1; count <= 4; ++count )
		for ( int from = -3; from <= 6; ++from )
			for ( int step = -2; step <= 2; ++step )
			{
				const int at = ChoiceStep( from, count, step );
				EXPECT_GE( at, 0 ) << count << "," << from << "," << step;
				EXPECT_LT( at, count ) << count << "," << from << "," << step;
			}
}

TEST( ChoiceRowStep, AnEmptyRowHasNowhereToGo )
{
	EXPECT_EQ( 0, ChoiceStep( 0, 0, 1 ));
}

// ---------------------------------------------------------------- the invariant

TEST( ChoiceRowNormalise, ThereIsAlwaysExactlyOneAnswer )
{
	// [rc4l] "None selected" is not a state this control has. Every caller has a default, and an
	// index that arrives out of range is brought back to it rather than honoured -- a question with
	// no answer showing is one the player cannot tell from a broken control.
	EXPECT_EQ( 0, ChoiceNormalise( -1, 2 ));
	EXPECT_EQ( 0, ChoiceNormalise( 99, 2 ));
	EXPECT_EQ( 0, ChoiceNormalise( 0, 0 ));

	EXPECT_EQ( 1, ChoiceNormalise( 1, 2 ));
}
