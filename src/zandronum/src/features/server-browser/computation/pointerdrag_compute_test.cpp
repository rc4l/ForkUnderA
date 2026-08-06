// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/pointerdrag_compute.h"

using zx::BeginDrag;
using zx::DragOutcome;
using zx::PointerEvent;
using zx::StepDrag;

namespace
{
const PointerEvent kEvents[] = { PointerEvent::Press, PointerEvent::Move, PointerEvent::Release };
const int kEventCount = 3;
} // namespace

// ---------------------------------------------------------------- the regression

TEST( PointerDrag, APressIsNeverEatenByADragThatNeverEnded )
{
	// [rc4l] THE BUG THIS UNIT EXISTS FOR, and it shipped.
	//
	// Both text fields tracked a drag with a bool set on press and cleared on release, consuming
	// every event in between so a selection could continue past the edge of the box. Correct only
	// while the release arrives -- and it does not when focus moves by keyboard between the two.
	//
	// The flag stayed set, and the NEXT CLICK WAS EATEN WHOLE: the press swallowed by a gesture that
	// had ended long before, the click that should have chosen something doing nothing, and the one
	// after it working. That reads as a flaky menu rather than a rule with a hole in it.
	const DragOutcome out = StepDrag( true, PointerEvent::Press );

	EXPECT_FALSE( out.consumed );
	EXPECT_FALSE( out.dragging );
}

TEST( PointerDrag, ALostReleaseCostsAFrameNotAClick )
{
	// The sequence that broke it, start to finish: press on a field, the release goes elsewhere, and
	// then a press on something else. That last press must reach what is under it.
	bool dragging = BeginDrag( );			// press landed on a field
	ASSERT_TRUE( dragging );

	// ... the release never arrives; focus moved by keyboard ...

	const DragOutcome next = StepDrag( dragging, PointerEvent::Press );

	EXPECT_FALSE( next.consumed ) << "the press was swallowed by a gesture that had already ended";
	EXPECT_FALSE( next.dragging );
}

// ---------------------------------------------------------------- ordinary dragging

TEST( PointerDrag, AMoveDuringAGestureBelongsToIt )
{
	// This is what lets a selection continue once the pointer leaves the box -- a drag that stopped
	// the moment you overshot the last character is one you could never make in a single gesture.
	const DragOutcome out = StepDrag( true, PointerEvent::Move );

	EXPECT_TRUE( out.consumed );
	EXPECT_TRUE( out.dragging );
}

TEST( PointerDrag, AReleaseEndsTheGestureAndIsClaimedByIt )
{
	const DragOutcome out = StepDrag( true, PointerEvent::Release );

	EXPECT_TRUE( out.consumed );
	EXPECT_FALSE( out.dragging );
}

// ---------------------------------------------------------------- when nothing is being dragged

TEST( PointerDrag, NothingIsClaimedWhenNoGestureIsRunning )
{
	// A hover must reach the control it is over, and a release nobody asked for must not be
	// swallowed before whatever is under it can see it.
	for ( int e = 0; e < kEventCount; ++e )
	{
		const DragOutcome out = StepDrag( false, kEvents[e] );
		EXPECT_FALSE( out.consumed ) << e;
	}
}

TEST( PointerDrag, IdleStaysIdle )
{
	for ( int e = 0; e < kEventCount; ++e )
		EXPECT_FALSE( StepDrag( false, kEvents[e] ).dragging ) << e;
}

// ---------------------------------------------------------------- the invariants

TEST( PointerDrag, OnlyAMoveCanEverLeaveAGestureRunning )
{
	// Every other event ends it. That is what bounds how long a stale flag can survive: one move at
	// most, and a move only arrives while the pointer is actually being moved.
	for ( int d = 0; d < 2; ++d )
		for ( int e = 0; e < kEventCount; ++e )
		{
			const DragOutcome out = StepDrag( d != 0, kEvents[e] );
			if ( out.dragging )
				EXPECT_EQ( PointerEvent::Move, kEvents[e] ) << d << "," << e;
		}
}

TEST( PointerDrag, NothingIsEverConsumedWithoutAGestureToConsumeIt )
{
	// Swept, because "consumed" makes the caller drop the event -- and an event dropped with no
	// gesture behind it is a control that does not respond.
	for ( int d = 0; d < 2; ++d )
		for ( int e = 0; e < kEventCount; ++e )
		{
			const DragOutcome out = StepDrag( d != 0, kEvents[e] );
			if ( out.consumed )
				EXPECT_TRUE( d != 0 ) << d << "," << e;
		}
}

TEST( PointerDrag, APressNeverStartsOneByItself )
{
	// Starting is the CALLER's decision -- only it knows whether the thing under the pointer is
	// draggable -- and it makes that decision after the press has ended whatever came before.
	for ( int d = 0; d < 2; ++d )
		EXPECT_FALSE( StepDrag( d != 0, PointerEvent::Press ).dragging ) << d;

	EXPECT_TRUE( BeginDrag( ));
}
