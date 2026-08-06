// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Moving the focus glow from where it was to where it now belongs, along a curve.
//
// A marker that teleports has to be FOUND again after every keypress. One that travels is followed,
// because the eye tracks motion without being asked to.
//
// WHY A JOURNEY RATHER THAN A CHASE. The first version simply moved a fraction of the remaining
// distance each tic -- exponential smoothing, which eases beautifully and can only ever go in a
// straight line, because it has no memory of where it set out from. A curve needs three things a
// chase does not have: a start, an end, and how far along it is. So that is what this holds.
//
// THE CURVE is a quadratic Bezier whose control point sits off to one side of the straight line, so
// the glow bows out and comes back rather than sliding flat. The bow is proportional to the distance
// travelled and capped, which means a hop between two rows is very nearly straight -- an arc on a
// twelve-pixel move would read as a wobble -- while crossing the panel visibly swings.
//
// THE PROGRESS is smoothstepped -- eased at BOTH ends, not just the far one. A pure ease-out leaves
// at full speed, and a marker that jumps into motion the instant a key goes down is jarring in the
// same way teleporting is: the eye catches the start instead of following it. Accelerating out of
// rest and decelerating into the destination makes the whole move read as one gesture.
//
// Duration scales with distance and is clamped at both ends: short moves must not take as long as
// long ones, and long moves must not take longer than the player's next keypress.
//
// TIMED IN MILLISECONDS, NOT TICS. Stepping this once per 35Hz tic made the motion visibly steppy on
// any display worth having -- 35 positions a second is a slideshow next to a 144Hz panel. Stepping
// it once per FRAME instead would be smooth but would tie the speed to the frame rate, which is the
// bug that moving it to the tic fixed. Real elapsed time is the only clock that is both smooth and
// honest: the journey takes the same wall-clock time everywhere, and it is evaluated as often as
// there are frames to draw it on.
//
// Integer arithmetic throughout, in thousandths, so the same input gives the same pixel on every
// machine and every test run.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_GLOWTRAVEL_COMPUTE_H
#define ZX_GLOWTRAVEL_COMPUTE_H

namespace zx
{

struct GlowPos
{
	int x, y;

	GlowPos() : x(0), y(0) {}
	GlowPos(int px, int py) : x(px), y(py) {}
};

struct GlowTravel
{
	GlowPos from;
	GlowPos to;
	int elapsed;		// milliseconds since it set out
	int duration;		// milliseconds the whole journey takes; never zero

	GlowTravel() : elapsed(0), duration(1) {}
};

// Set out from `at` towards `to`. Called again mid-flight when the focus moves on -- passing the
// glow's CURRENT position as `at` is what makes a change of mind continue smoothly instead of
// snapping back to wherever the last journey began.
GlowTravel BeginGlowTravel( GlowPos at, GlowPos to );

// `deltaMs` milliseconds older. Stops at the duration rather than running past it.
//
// A silly delta is clamped rather than trusted: alt-tabbing away, a level load or a breakpoint all
// hand back an enormous elapsed time, and a glow that teleports because the game stopped drawing is
// the exact thing this exists to avoid.
GlowTravel StepGlowTravel( const GlowTravel &travel, int deltaMs );

// Where the glow is now: a point on the bowed curve at the eased progress.
GlowPos GlowTravelPoint( const GlowTravel &travel );

bool GlowTravelDone( const GlowTravel &travel );

} // namespace zx

#endif // ZX_GLOWTRAVEL_COMPUTE_H
