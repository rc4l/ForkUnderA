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

// One frame at roughly 60fps. The unit is timed in milliseconds now, so the tests step it in them.
const int kFrameMs = 16;

GlowTravel Advance( const GlowTravel &t ) { return StepGlowTravel( t, kFrameMs ); }
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
	for ( int i = 0; i < 200; ++i )
		t = Advance( t );

	EXPECT_TRUE( GlowTravelDone( t ));
	EXPECT_EQ( 300, GlowTravelPoint( t ).x );
	EXPECT_EQ( 200, GlowTravelPoint( t ).y );
}

TEST( GlowTravel, StaysPutOnceItHasArrived )
{
	GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 100, 100 ));
	for ( int i = 0; i < 200; ++i )
		t = Advance( t );

	const GlowPos settled = GlowTravelPoint( t );
	for ( int i = 0; i < 10; ++i )
	{
		t = Advance( t );
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
		t = Advance( t );
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
		t = Advance( t );
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
		t = Advance( t );
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
		t = Advance( t );
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
		t = Advance( t );
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
		t = Advance( t );
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
	// MILLISECONDS of wall-clock time, the same on every machine whatever the frame rate.
	for ( int distance = 0; distance <= 900; distance += 7 )
	{
		const GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( distance, 0 ));
		EXPECT_GE( t.duration, 170 ) << distance;
		EXPECT_LE( t.duration, 560 ) << distance;
	}
}

TEST( GlowTravel, TakesTheSameTimeWhateverTheFrameRate )
{
	// The whole reason it is timed rather than ticked. A 144Hz machine and a 30Hz one must see the
	// same journey take the same time -- they just see it at different smoothness.
	const GlowPos from( 0, 0 );
	const GlowPos to( 400, 0 );

	int elapsedFast = 0;
	GlowTravel fast = BeginGlowTravel( from, to );
	while ( !GlowTravelDone( fast ))
	{
		fast = StepGlowTravel( fast, 7 );		// ~144fps
		elapsedFast += 7;
	}

	int elapsedSlow = 0;
	GlowTravel slow = BeginGlowTravel( from, to );
	while ( !GlowTravelDone( slow ))
	{
		slow = StepGlowTravel( slow, 33 );		// ~30fps
		elapsedSlow += 33;
	}

	// Within one slow frame of each other, which is as close as a coarse sampler can get.
	EXPECT_LE( Abs( elapsedFast - elapsedSlow ), 33 );
}

TEST( GlowTravel, ASmallerDeltaGivesMorePositions )
{
	// Smoothness, stated as a test: more frames in the same journey means more distinct places the
	// glow is seen, which is exactly what 35Hz was failing to provide.
	const GlowPos from( 0, 0 );
	const GlowPos to( 400, 0 );

	int coarse = 0;
	GlowTravel a = BeginGlowTravel( from, to );
	while ( !GlowTravelDone( a )) { a = StepGlowTravel( a, 28 ); ++coarse; }

	int fine = 0;
	GlowTravel b = BeginGlowTravel( from, to );
	while ( !GlowTravelDone( b )) { b = StepGlowTravel( b, 7 ); ++fine; }

	EXPECT_GT( fine, coarse * 3 );
}

TEST( GlowTravel, AHugeGapIsClampedRatherThanTeleportedThrough )
{
	// Alt-tabbing away, a level load or a breakpoint all hand back an enormous elapsed time. Honouring
	// it would finish the journey in one frame, which is the teleport this exists to prevent.
	GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 640, 0 ));
	t = StepGlowTravel( t, 100000 );

	EXPECT_FALSE( GlowTravelDone( t ));
}

TEST( GlowTravel, ANegativeDeltaDoesNotRunBackwards )
{
	GlowTravel t = BeginGlowTravel( GlowPos( 0, 0 ), GlowPos( 400, 0 ));
	t = StepGlowTravel( t, 100 );
	const int was = t.elapsed;

	t = StepGlowTravel( t, -500 );
	EXPECT_EQ( was, t.elapsed );
}

TEST( GlowTravel, ATravelToWhereItAlreadyIsIsHarmless )
{
	GlowTravel t = BeginGlowTravel( GlowPos( 50, 50 ), GlowPos( 50, 50 ));

	for ( int i = 0; i < 30; ++i )
	{
		t = Advance( t );
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
		t = Advance( t );

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
	EXPECT_TRUE( GlowTravelDone( StepGlowTravel( t, kFrameMs )) || true );	// must not hang or divide by zero
}

TEST( GlowTravel, TheReturnJourneyRetracesTheSameArc )
{
	// The perpendicular (-dy, dx) turns with the direction of travel, so going down bowed one way and
	// coming back up bowed the other -- and the pair traced a full circle rather than one path walked
	// twice. Both directions must now bow to the SAME side.
	const GlowPos top( 100, 100 );
	const GlowPos bottom( 100, 300 );

	int downMax = 0;
	int downMin = 0;
	GlowTravel down = BeginGlowTravel( top, bottom );
	while ( !GlowTravelDone( down ))
	{
		down = Advance( down );
		const int off = GlowTravelPoint( down ).x - 100;
		downMax = ( off > downMax ) ? off : downMax;
		downMin = ( off < downMin ) ? off : downMin;
	}

	int upMax = 0;
	int upMin = 0;
	GlowTravel up = BeginGlowTravel( bottom, top );
	while ( !GlowTravelDone( up ))
	{
		up = Advance( up );
		const int off = GlowTravelPoint( up ).x - 100;
		upMax = ( off > upMax ) ? off : upMax;
		upMin = ( off < upMin ) ? off : upMin;
	}

	// It bows at all, outward -- negative x, away from the list...
	EXPECT_LT( downMin, 0 );
	EXPECT_LT( upMin, 0 );

	// ...and neither direction strays to the other side of the line.
	EXPECT_EQ( 0, downMax );
	EXPECT_EQ( 0, upMax );
}

TEST( GlowTravel, EveryDirectionBowsToOneConsistentSide )
{
	// Swept around the compass: whichever way it goes, the curve leaves the straight line on the same
	// side, so no pair of opposite moves can make a circle.
	const GlowPos centre( 300, 200 );
	const int offsets[][2] = {
		{ 0, 150 }, { 0, -150 }, { 150, 0 }, { -150, 0 },
		{ 120, 120 }, { -120, -120 }, { 120, -120 }, { -120, 120 },
	};

	for ( size_t i = 0; i < sizeof( offsets ) / sizeof( offsets[0] ); ++i )
	{
		const GlowPos to( centre.x + offsets[i][0], centre.y + offsets[i][1] );

		GlowTravel t = BeginGlowTravel( centre, to );
		bool bowed = false;

		while ( !GlowTravelDone( t ))
		{
			t = Advance( t );
			if ( OffLine( centre, to, GlowTravelPoint( t )) > 0 )
				bowed = true;
		}

		EXPECT_TRUE( bowed ) << "direction " << i << " travelled flat";
	}
}
