// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/glowtravel_compute.h"

namespace zx
{

namespace
{
const int kOne = 1000;			// progress is in thousandths

// How long a journey takes, in tics, from how far it goes.
//
// Clamped at both ends and deliberately sub-linear: a hop between two rows must not take as long as
// crossing the panel, and crossing the panel must not take longer than the player's next keypress.
int DurationFor( int distance )
{
	int tics = 6 + distance / 24;

	if ( tics < 6 )
		tics = 6;
	if ( tics > 20 )
		tics = 20;

	return tics;
}

// Straight-line distance, near enough. The octagonal approximation -- larger + 3/8 of smaller -- is
// within a few percent of the true hypotenuse and needs no square root, and this only ever feeds a
// duration and a bow depth, neither of which anyone can measure by eye.
int RoughDistance( int dx, int dy )
{
	const int ax = ( dx < 0 ) ? -dx : dx;
	const int ay = ( dy < 0 ) ? -dy : dy;
	const int large = ( ax > ay ) ? ax : ay;
	const int small = ( ax > ay ) ? ay : ax;

	return large + ( small * 3 ) / 8;
}

// Eased progress: SMOOTHSTEP, t^2(3 - 2t), in thousandths.
//
// Smoothed at BOTH ends, not just the far one. A pure ease-out leaves at full speed, and a marker
// that jumps into motion the instant a key goes down is jarring in the same way teleporting is --
// the eye catches the start rather than following it. Smoothstep accelerates out of rest, cruises
// through the middle where the curve's bow is widest, and decelerates into the destination, which
// is what makes the whole move read as one gesture instead of a launch and a drift.
int Smoothstep( int t )
{
	if ( t <= 0 )
		return 0;
	if ( t >= kOne )
		return kOne;

	// t*t*(3 - 2t) with everything kept in thousandths. Long long because the numerator reaches
	// 1000 * 1000 * 3000, which is comfortably past what an int holds.
	const long long tt = static_cast<long long>( t ) * t;
	return static_cast<int>(( tt * ( 3 * kOne - 2 * t )) / ( static_cast<long long>( kOne ) * kOne ));
}

// One axis of a quadratic Bezier at eased progress `t`.
long long Bezier( long long p0, long long control, long long p1, long long t )
{
	const long long u = kOne - t;

	return ( u * u * p0 + 2 * u * t * control + t * t * p1 ) / ( static_cast<long long>( kOne ) * kOne );
}
} // namespace

GlowTravel BeginGlowTravel( GlowPos at, GlowPos to )
{
	GlowTravel out;
	out.from = at;
	out.to = to;
	out.elapsed = 0;
	out.duration = DurationFor( RoughDistance( to.x - at.x, to.y - at.y ));
	return out;
}

GlowTravel StepGlowTravel( const GlowTravel &travel )
{
	GlowTravel out = travel;

	if ( out.duration < 1 )
		out.duration = 1;
	if ( out.elapsed < out.duration )
		++out.elapsed;

	return out;
}

GlowPos GlowTravelPoint( const GlowTravel &travel )
{
	const int duration = ( travel.duration < 1 ) ? 1 : travel.duration;

	int elapsed = travel.elapsed;
	if ( elapsed < 0 )
		elapsed = 0;
	if ( elapsed > duration )
		elapsed = duration;

	// Finished journeys report the destination exactly, rather than whatever the arithmetic rounds
	// to. A marker that settles one pixel off its mark is the bug the old floor existed to prevent.
	if ( elapsed >= duration )
		return travel.to;

	const int t = Smoothstep(( elapsed * kOne ) / duration );

	const int dx = travel.to.x - travel.from.x;
	const int dy = travel.to.y - travel.from.y;
	const int distance = RoughDistance( dx, dy );

	// The control point: the midpoint, pushed off at a right angle to the direction of travel. That
	// perpendicular is (-dy, dx), scaled so the bow is a fixed fraction of the distance and capped so
	// a long journey does not swing halfway across the screen.
	int bow = distance / 4;
	if ( bow > 28 )
		bow = 28;

	int offsetX = 0;
	int offsetY = 0;
	if ( distance > 0 )
	{
		offsetX = ( -dy * bow ) / distance;
		offsetY = ( dx * bow ) / distance;
	}

	const long long controlX = travel.from.x + dx / 2 + offsetX;
	const long long controlY = travel.from.y + dy / 2 + offsetY;

	return GlowPos( static_cast<int>( Bezier( travel.from.x, controlX, travel.to.x, t )),
		static_cast<int>( Bezier( travel.from.y, controlY, travel.to.y, t )));
}

bool GlowTravelDone( const GlowTravel &travel )
{
	return travel.elapsed >= (( travel.duration < 1 ) ? 1 : travel.duration );
}

} // namespace zx
