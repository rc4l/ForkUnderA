// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/continue/computation/continuelist_compute.h"

using namespace zx;

namespace
{

const int kPage = 8;

} // namespace

TEST( ContinueList, TheArrowsWalkTheList )
{
	EXPECT_EQ( 1, StepContinueList( ContinueListKey::Down, 0, 10, kPage ));
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::Up, 1, 10, kPage ));
}

TEST( ContinueList, TheArrowsWrapAtBothEnds )
{
	// Up from the first row is the fastest way to the last, and every list in this engine already
	// does it.
	EXPECT_EQ( 9, StepContinueList( ContinueListKey::Up, 0, 10, kPage ));
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::Down, 9, 10, kPage ));
}

TEST( ContinueList, ThePageKeysMoveAScreenful )
{
	EXPECT_EQ( 8, StepContinueList( ContinueListKey::PageDown, 0, 50, kPage ));
	EXPECT_EQ( 2, StepContinueList( ContinueListKey::PageUp, 10, 50, kPage ));
}

TEST( ContinueList, ThePageKeysStopAtTheEndsRatherThanWrapping )
{
	// A page key that wrapped would make HOLDING it a loop through the whole list, which is exactly
	// what somebody paging through fifty rows looking for one is not asking for.
	EXPECT_EQ( 49, StepContinueList( ContinueListKey::PageDown, 49, 50, kPage ));
	EXPECT_EQ( 49, StepContinueList( ContinueListKey::PageDown, 45, 50, kPage ));
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::PageUp, 0, 50, kPage ));
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::PageUp, 3, 50, kPage ));
}

TEST( ContinueList, HomeAndEndGoStraightThere )
{
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::Home, 27, 50, kPage ));
	EXPECT_EQ( 49, StepContinueList( ContinueListKey::End, 27, 50, kPage ));

	// And from where they already are, which is where an off-by-one would show.
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::Home, 0, 50, kPage ));
	EXPECT_EQ( 49, StepContinueList( ContinueListKey::End, 49, 50, kPage ));
}

TEST( ContinueList, EveryKeyAnswersSomethingUsableForAnEmptyList )
{
	// The caller uses the answer as an index, so "no rows" must not come back as -1.
	const ContinueListKey keys[] = { ContinueListKey::Up, ContinueListKey::Down,
		ContinueListKey::PageUp, ContinueListKey::PageDown, ContinueListKey::Home,
		ContinueListKey::End };

	for ( int i = 0; i < 6; ++i )
		EXPECT_EQ( 0, StepContinueList( keys[i], 0, 0, kPage ));
}

TEST( ContinueList, ASelectionFromBeforeTheListChangedIsPulledBackIn )
{
	// The history shortens under an open menu when an entry stops being usable, so the row the
	// cursor was on can simply cease to exist.
	// Repaired first, then moved: row 99 of a three-row list is the last row, and Down from the last
	// row is the wrap. What must never happen is the answer staying at 99.
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::Down, 99, 3, kPage ));
	EXPECT_EQ( 1, StepContinueList( ContinueListKey::Up, 99, 3, kPage ));
	EXPECT_EQ( 2, StepContinueList( ContinueListKey::End, -5, 3, kPage ));
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::PageUp, -5, 3, kPage ));
}

TEST( ContinueList, AListWithOneRowStaysOnIt )
{
	const ContinueListKey keys[] = { ContinueListKey::Up, ContinueListKey::Down,
		ContinueListKey::PageUp, ContinueListKey::PageDown, ContinueListKey::Home,
		ContinueListKey::End };

	for ( int i = 0; i < 6; ++i )
		EXPECT_EQ( 0, StepContinueList( keys[i], 0, 1, kPage ));
}

TEST( ContinueList, AViewportThatHoldsNothingStillMoves )
{
	// A window too short to report a whole row must not turn the page keys into keys that do
	// nothing at all.
	EXPECT_EQ( 1, StepContinueList( ContinueListKey::PageDown, 0, 10, 0 ));
	EXPECT_EQ( 0, StepContinueList( ContinueListKey::PageUp, 1, 10, -3 ));
}

TEST( ContinueList, TheVisibleRowsAreWhatFits )
{
	EXPECT_EQ( 8, ComputeContinueVisibleRows( 80, 10 ));
	EXPECT_EQ( 8, ComputeContinueVisibleRows( 89, 10 ));
	EXPECT_EQ( 9, ComputeContinueVisibleRows( 90, 10 ));
}

TEST( ContinueList, ThereIsAlwaysAtLeastOneVisibleRow )
{
	// Otherwise the keyboard walks a list that never appears to change.
	EXPECT_EQ( 1, ComputeContinueVisibleRows( 4, 10 ));
	EXPECT_EQ( 1, ComputeContinueVisibleRows( 0, 10 ));
	EXPECT_EQ( 1, ComputeContinueVisibleRows( 100, 0 ));
	EXPECT_EQ( 1, ComputeContinueVisibleRows( 100, -2 ));
}
