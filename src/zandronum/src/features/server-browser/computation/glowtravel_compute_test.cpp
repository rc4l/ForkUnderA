// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/glowtravel_compute.h"

using zx::AdvanceGlow;
using zx::GlowArrived;
using zx::GlowPos;

namespace
{
const int kNum = 1;
const int kDen = 3;		// a third of what is left, each tic

GlowPos Step( GlowPos at, GlowPos to ) { return AdvanceGlow( at, to, kNum, kDen ); }

// Runs the travel to completion and says how many tics it took, or -1 if it never arrived.
int TicsToArrive( GlowPos at, GlowPos to, int limit = 500 )
{
	for ( int i = 1; i <= limit; ++i )
	{
		at = Step( at, to );
		if ( GlowArrived( at, to ))
			return i;
	}
	return -1;
}
} // namespace

TEST( GlowTravel, MovesTowardsTheTarget )
{
	const GlowPos at = Step( GlowPos( 0, 0 ), GlowPos( 90, 30 ));

	EXPECT_GT( at.x, 0 );
	EXPECT_LE( at.x, 90 );
	EXPECT_GT( at.y, 0 );
	EXPECT_LE( at.y, 30 );
}

TEST( GlowTravel, EasesOutRatherThanTravellingLinearly )
{
	// Each step must be no larger than the one before: it leaves fast and settles slowly, which is
	// what stops a two-pixel hop taking as long as crossing the panel.
	GlowPos at( 0, 0 );
	const GlowPos to( 300, 0 );

	int previous = 1 << 30;
	for ( int i = 0; i < 12; ++i )
	{
		const GlowPos next = Step( at, to );
		const int moved = next.x - at.x;
		if ( moved == 0 )
			break;

		EXPECT_LE( moved, previous ) << "step " << i << " grew";
		previous = moved;
		at = next;
	}
}

TEST( GlowTravel, AlwaysArrives )
{
	// The floor is what makes this true. A pure fraction is an asymptote: it would creep for ever and
	// leave the marker a pixel off its mark indefinitely.
	EXPECT_GT( TicsToArrive( GlowPos( 0, 0 ), GlowPos( 1, 0 )), 0 );
	EXPECT_GT( TicsToArrive( GlowPos( 0, 0 ), GlowPos( 640, 400 )), 0 );
	EXPECT_GT( TicsToArrive( GlowPos( 640, 400 ), GlowPos( 0, 0 )), 0 );
	EXPECT_GT( TicsToArrive( GlowPos( -50, 700 ), GlowPos( 12, -9 )), 0 );
}

TEST( GlowTravel, ArrivesQuicklyEnoughToFeelResponsive )
{
	// Across the whole virtual screen, in well under a second at 35 tics a second. A marker that takes
	// longer than the next keypress is a marker that is always behind the player.
	EXPECT_LE( TicsToArrive( GlowPos( 0, 0 ), GlowPos( 640, 400 )), 25 );
}

TEST( GlowTravel, ArrivingIsStable )
{
	// Once there, it stays. A step that overshot would sit and oscillate around the target for ever.
	const GlowPos to( 100, 50 );
	GlowPos at = to;

	for ( int i = 0; i < 10; ++i )
	{
		at = Step( at, to );
		EXPECT_EQ( to.x, at.x );
		EXPECT_EQ( to.y, at.y );
	}
}

TEST( GlowTravel, NeverOvershoots )
{
	// Swept, because an overshoot only shows on particular distances and reads as a wobble rather
	// than as a bug worth reporting.
	for ( int distance = 1; distance <= 400; ++distance )
	{
		GlowPos at( 0, 0 );
		const GlowPos to( distance, 0 );

		for ( int i = 0; i < 200; ++i )
		{
			at = Step( at, to );
			EXPECT_GE( at.x, 0 ) << distance;
			EXPECT_LE( at.x, distance ) << distance;
			if ( GlowArrived( at, to ))
				break;
		}
	}
}

TEST( GlowTravel, TravelsBothWays )
{
	EXPECT_LT( Step( GlowPos( 100, 100 ), GlowPos( 0, 0 )).x, 100 );
	EXPECT_LT( Step( GlowPos( 100, 100 ), GlowPos( 0, 0 )).y, 100 );
	EXPECT_GT( Step( GlowPos( -100, -100 ), GlowPos( 0, 0 )).x, -100 );
}

TEST( GlowTravel, BothAxesArriveTogetherOrCloseTo )
{
	// One axis finishing many tics before the other turns a diagonal slide into an L-shape.
	const int x = TicsToArrive( GlowPos( 0, 0 ), GlowPos( 200, 0 ));
	const int y = TicsToArrive( GlowPos( 0, 0 ), GlowPos( 0, 200 ));
	EXPECT_EQ( x, y );
}

TEST( GlowTravel, ANonsenseFractionSnapsRatherThanMisbehaving )
{
	// A marker in the wrong place is a worse failure than one that did not animate.
	const GlowPos to( 42, 24 );

	EXPECT_TRUE( GlowArrived( AdvanceGlow( GlowPos( 0, 0 ), to, 1, 0 ), to ));
	EXPECT_TRUE( GlowArrived( AdvanceGlow( GlowPos( 0, 0 ), to, 0, 3 ), to ));
	EXPECT_TRUE( GlowArrived( AdvanceGlow( GlowPos( 0, 0 ), to, 5, 3 ), to ));
	EXPECT_TRUE( GlowArrived( AdvanceGlow( GlowPos( 0, 0 ), to, -1, 3 ), to ));
}

TEST( GlowTravel, AFullFractionArrivesImmediately )
{
	const GlowPos to( 42, 24 );
	EXPECT_TRUE( GlowArrived( AdvanceGlow( GlowPos( 0, 0 ), to, 3, 3 ), to ));
}
