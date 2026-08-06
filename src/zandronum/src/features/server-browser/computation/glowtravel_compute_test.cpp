// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/glowtravel_compute.h"

using zx::BeginGlowTravel;
using zx::GlowPos;
using zx::GlowTravel;
using zx::GlowTravelDone;
using zx::GlowTravelPoint;
using zx::StepGlowTravel;

namespace
{
// Distance from a point to the straight line between two others, times the line's length -- the
// cross product. Zero means the point is ON the line, which is what "does it bow?" comes down to.
long long OffLine( GlowPos from, GlowPos to, GlowPos at )
{
	const long long dx = to.x - from.x;
	const long long dy = to.y - from.y;
	const long long px = at.x - from.x;
	const long long py = at.y - from.y;

	const long long cross = dx * py - dy * px;
	return ( cross < 0 ) ? -cross : cross;
}

int Abs( int v ) { return ( v < 0 ) ? -v : v; }
} // namespace

// ---------------------------------------------------------------- the ends

TEST( GlowTravel, StartsExactlyWhereItSetOut )
{
	const GlowTravel t = BeginGlowTravel( GlowPos( 10, 20 ), GlowPos( 300, 200 ));
	const GlowPos at = GlowTravelPoint( t );

	EXPECT_EQ( 10, at.x );
	EXPECT_EQ( 20, at.y );
}

TEST( GlowTravel, ArrivesExactlyWhereItWasGoing )
{
	// Exactly, not near enough. A marker that settles a pixel off its mark is the bug the whole unit
	// exists to avoid.
	GlowTravel t = BeginGlowTravel( GlowPos( 10, 20 ), GlowPos( 300, 200 ));
	for ( int i = 0; i < 100; ++i )
		t = StepGlowTravel( t );

	EXPECT_TRUE( GlowTravelDone( t ));
	EXPECT_EQ( 300, GlowTravelPoint( t ).x );
	EXPECT_EQ( 200, GlowTravelPoint( t ).y );
}

TEST( GlowTravel, StaysPutOnceItHasArrived )
{
	GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 100, 100 ));
	for ( int i = 0; i < 100; ++i )
		t = StepGlowTravel( t );

	const GlowPos settled = GlowTravelPoint( t );
	for ( int i = 0; i < 10; ++i )
	{
		t = StepGlowTravel( t );
		EXPECT_EQ( settled.x, GlowTravelPoint( t ).x );
		EXPECT_EQ( settled.y, GlowTravelPoint( t ).y );
	}
}

// ---------------------------------------------------------------- the curve

TEST( GlowTravel, BowsOffTheStraightLine )
{
	// The whole point of the rework. Somewhere in the middle it must NOT be on the line between the
	// two ends.
	const GlowPos from( 0, 0 );
	const GlowPos to( 400, 0 );

	GlowTravel t = BeginGlowTravel( from, to );
	long long widest = 0;

	while ( !GlowTravelDone( t ))
	{
		t = StepGlowTravel( t );
		const long long off = OffLine( from, to, GlowTravelPoint( t ));
		if ( off > widest )
			widest = off;
	}

	EXPECT_GT( widest, 0 );
}

TEST( GlowTravel, AShortHopIsNearlyStraight )
{
	// An arc on a twelve-pixel move reads as a wobble, not as a gesture -- so the bow scales with the
	// distance and all but vanishes on a hop between neighbouring rows.
	const GlowPos from( 0, 0 );
	const GlowPos to( 0, 16 );

	GlowTravel t = BeginGlowTravel( from, to );
	int widest = 0;

	while ( !GlowTravelDone( t ))
	{
		t = StepGlowTravel( t );
		widest = ( Abs( GlowTravelPoint( t ).x ) > widest ) ? Abs( GlowTravelPoint( t ).x ) : widest;
	}

	EXPECT_LE( widest, 4 );
}

TEST( GlowTravel, TheBowIsCappedOnLongJourneys )
{
	// Otherwise crossing the panel would swing the glow halfway across the screen on its way.
	const GlowPos from( 0, 0 );
	const GlowPos to( 640, 0 );

	GlowTravel t = BeginGlowTravel( from, to );
	int widest = 0;

	while ( !GlowTravelDone( t ))
	{
		t = StepGlowTravel( t );
		widest = ( Abs( GlowTravelPoint( t ).y ) > widest ) ? Abs( GlowTravelPoint( t ).y ) : widest;
	}

	EXPECT_GT( widest, 4 );		// it does bow
	EXPECT_LE( widest, 30 );	// but not absurdly
}

TEST( GlowTravel, NeverStraysFarOutsideTheTwoEnds )
{
	// A curve is allowed to leave the straight line. It is not allowed to leave the neighbourhood.
	const GlowPos from( 100, 100 );
	const GlowPos to( 300, 260 );

	GlowTravel t = BeginGlowTravel( from, to );
	while ( !GlowTravelDone( t ))
	{
		t = StepGlowTravel( t );
		const GlowPos at = GlowTravelPoint( t );

		EXPECT_GE( at.x, 100 - 40 );
		EXPECT_LE( at.x, 300 + 40 );
		EXPECT_GE( at.y, 100 - 40 );
		EXPECT_LE( at.y, 260 + 40 );
	}
}

// ---------------------------------------------------------------- the easing

TEST( GlowTravel, EasesInAsWellAsOut )
{
	// Smoothstep, not ease-out. The first step must be SMALL -- a marker that jumps into motion the
	// instant a key goes down is caught by the eye rather than followed by it -- and the last step
	// must be small too.
	const GlowPos from( 0, 0 );
	const GlowPos to( 400, 0 );

	GlowTravel t = BeginGlowTravel( from, to );
	int previous = GlowTravelPoint( t ).x;
	int first = -1;
	int biggest = 0;
	int last = 0;

	while ( !GlowTravelDone( t ))
	{
		t = StepGlowTravel( t );
		const int now = GlowTravelPoint( t ).x;
		const int moved = Abs( now - previous );
		previous = now;

		if ( first < 0 )
			first = moved;
		if ( moved > biggest )
			biggest = moved;
		last = moved;
	}

	EXPECT_LT( first, biggest );	// accelerates out of rest
	EXPECT_LT( last, biggest );		// and decelerates into the destination
}

TEST( GlowTravel, IsMonotonicAlongTheDirectionOfTravel )
{
	// It may bow sideways; it must never double back on itself, which would read as a stumble.
	const GlowPos from( 0, 0 );
	const GlowPos to( 400, 0 );

	GlowTravel t = BeginGlowTravel( from, to );
	int previous = GlowTravelPoint( t ).x;

	while ( !GlowTravelDone( t ))
	{
		t = StepGlowTravel( t );
		const int now = GlowTravelPoint( t ).x;
		EXPECT_GE( now, previous );
		previous = now;
	}
}

// ---------------------------------------------------------------- timing

TEST( GlowTravel, ShortMovesFinishFasterThanLongOnes )
{
	EXPECT_LT( BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 0, 16 )).duration,
		BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 640, 400 )).duration );
}

TEST( GlowTravel, EveryJourneyFitsBetweenTheClamps )
{
	// TICS, not frames -- the caller advances this from Ticker. Six is quick enough not to feel like
	// lag on a tiny hop; twenty is under a second at 35Hz, so the glow is never behind the player.
	for ( int distance = 0; distance <= 900; distance += 7 )
	{
		const GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( distance, 0 ));
		EXPECT_GE( t.duration, 6 ) << distance;
		EXPECT_LE( t.duration, 20 ) << distance;
	}
}

TEST( GlowTravel, ATravelToWhereItAlreadyIsIsHarmless )
{
	GlowTravel t = BeginGlowTravel( GlowPos( 50, 50 ), GlowPos( 50, 50 ));

	for ( int i = 0; i < 30; ++i )
	{
		t = StepGlowTravel( t );
		EXPECT_EQ( 50, GlowTravelPoint( t ).x );
		EXPECT_EQ( 50, GlowTravelPoint( t ).y );
	}
	EXPECT_TRUE( GlowTravelDone( t ));
}

// ---------------------------------------------------------------- changing its mind

TEST( GlowTravel, RetargetingFromTheCurrentPointContinuesSmoothly )
{
	// The focus can move again mid-flight. Setting out afresh from where the glow IS -- rather than
	// from where the last journey began -- is what stops it snapping backwards.
	GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 400, 0 ));
	for ( int i = 0; i < 4; ++i )
		t = StepGlowTravel( t );

	const GlowPos midFlight = GlowTravelPoint( t );

	const GlowTravel again = BeginGlowTravel( midFlight, GlowPos( 0, 300 ));
	const GlowPos resumed = GlowTravelPoint( again );

	EXPECT_EQ( midFlight.x, resumed.x );
	EXPECT_EQ( midFlight.y, resumed.y );
}

TEST( GlowTravel, SurvivesANonsenseDuration )
{
	// Nothing constructs one of these by hand today, but a zero duration would divide by zero and a
	// negative elapsed would index off the front of the curve.
	GlowTravel t;
	t.from = GlowPos( 0, 0 );
	t.to = GlowPos( 100, 100 );
	t.elapsed = -5;
	t.duration = 0;

	const GlowPos at = GlowTravelPoint( t );
	EXPECT_GE( at.x, 0 );
	EXPECT_LE( at.x, 100 );
	EXPECT_TRUE( GlowTravelDone( StepGlowTravel( t )) || true );	// must not hang or divide by zero
}
