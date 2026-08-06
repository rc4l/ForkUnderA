// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Moving the focus glow from where it was to where it now belongs.
//
// A marker that teleports has to be FOUND again after every keypress. One that travels is followed,
// because the eye tracks motion without being asked to -- which is the entire reason Kingdom Hearts
// slides its cursor instead of blinking it from item to item, and the reason this exists rather than
// simply drawing at the new anchor.
//
// EASE-OUT, not linear: it covers a fixed FRACTION of whatever distance is left each tic, so it
// leaves fast and settles slowly. Linear travel reads mechanical and, worse, takes the same time
// across a two-pixel hop as across the whole panel.
//
// The floor is what makes it terminate. A pure fraction is an asymptote -- it would creep for ever
// and never arrive, leaving the glow a pixel off its mark indefinitely. So the step is at least one
// pixel, and anything within that distance snaps.
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

// One tic of travel from `at` towards `to`.
//
// `numerator`/`denominator` is the fraction of the remaining distance covered per tic -- 1/3 leaves
// briskly and settles in a handful of tics. A nonsense fraction (zero or negative denominator, or a
// numerator past the denominator) snaps rather than misbehaving: a marker in the wrong place is a
// worse failure than one that did not animate.
GlowPos AdvanceGlow( GlowPos at, GlowPos to, int numerator, int denominator );

// True once the glow is close enough that further travel would be invisible.
bool GlowArrived( GlowPos at, GlowPos to );

} // namespace zx

#endif // ZX_GLOWTRAVEL_COMPUTE_H
