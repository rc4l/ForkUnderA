// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/glowtravel_compute.h"

namespace zx
{

namespace
{
const int kOne = 1000;			// progress is in thousandths

// How long a journey takes, in MILLISECONDS, from how far it goes.
//
// Clamped at both ends and deliberately sub-linear: a hop between two rows must not take as long as
// crossing the panel, and crossing the panel must not take longer than the player's next keypress.
// 170ms is quick enough not to feel like lag on a tiny hop; 560ms is comfortably under a second.
int DurationFor( int distance )
{
	int ms = 170 + ( distance * 20 ) / 24;

	if ( ms < 170 )
		ms = 170;
	if ( ms > 560 )
		ms = 560;

	return ms;
}

// The largest step that is a real frame rather than an interruption. Past this the game was not
// drawing -- alt-tabbed, loading, paused at a breakpoint -- and honouring the gap would teleport the
// glow, which is the thing the travel exists to prevent.
const int kMaxStepMs = 200;

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

GlowTravel StepGlowTravel( const GlowTravel &travel, int deltaMs )
{
	GlowTravel out = travel;

	if ( out.duration < 1 )
		out.duration = 1;

	int step = deltaMs;
	if ( step < 0 )
		step = 0;
	if ( step > kMaxStepMs )
		step = kMaxStepMs;

	out.elapsed += step;
	if ( out.elapsed > out.duration )
		out.elapsed = out.duration;

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

	// [rc4l] THE BOW ALWAYS FALLS ON THE SAME SIDE, whichever way the glow is travelling.
	//
	// The perpendicular (-dy, dx) turns with the direction of travel, so going down bowed one way and
	// coming back up bowed the other -- and a down-then-up pair traced a full circle rather than
	// retracing one arc. Pinning the offset to a canonical side makes the return journey follow the
	// same curve backwards, which is what makes the movement feel like one path between two places
	// rather than an orbit around them.
	// The side is OUTWARD -- negative x, away from the list and towards the panel edge. Bowing the
	// other way sent the glow in under the rows it was travelling past, which reads as the light
	// ducking behind the content rather than swinging around it.
	if (( offsetX > 0 ) || (( offsetX == 0 ) && ( offsetY > 0 )))
	{
		offsetX = -offsetX;
		offsetY = -offsetY;
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
