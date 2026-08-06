// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/glowtravel_compute.h"

namespace zx
{

namespace
{
// One axis. Kept apart so the two cannot drift into different rules, which is how a diagonal slide
// ends up arriving on one axis several tics before the other.
int StepAxis( int at, int to, int numerator, int denominator )
{
	const int remaining = to - at;
	if ( remaining == 0 )
		return at;

	const int distance = ( remaining < 0 ) ? -remaining : remaining;
	int step = ( distance * numerator ) / denominator;

	// The floor. Without it the fraction is an asymptote and the glow creeps for ever, sitting a
	// pixel off its mark and never quite arriving.
	if ( step < 1 )
		step = 1;
	if ( step > distance )
		step = distance;

	return ( remaining < 0 ) ? ( at - step ) : ( at + step );
}
} // namespace

GlowPos AdvanceGlow( GlowPos at, GlowPos to, int numerator, int denominator )
{
	// A fraction that makes no sense would either stall or overshoot. Snapping is the honest failure:
	// the marker ends up in the right place, it just does not travel there.
	if (( denominator <= 0 ) || ( numerator <= 0 ) || ( numerator > denominator ))
		return to;

	return GlowPos( StepAxis( at.x, to.x, numerator, denominator ),
		StepAxis( at.y, to.y, numerator, denominator ));
}

bool GlowArrived( GlowPos at, GlowPos to )
{
	return ( at.x == to.x ) && ( at.y == to.y );
}

} // namespace zx
