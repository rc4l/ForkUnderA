// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Pure math for the damage-floor sprite tint (features/damage-tint): the intensity ramp, the
// damage-cycle pulse, and the white->floor-color blend. Header-pure and engine-free so it is
// unit-tested off-engine; the glue (damagetint.cpp) and the two GL seams stay trivial.
//
// The effect: a player standing on a damaging floor has their sprite (and, faintly, their own
// weapon sprite) tinted toward the floor texture's average color -- status readability in the
// Overwatch sense, derived from map content instead of a curated table.

#ifndef ZX_DAMAGETINT_COMPUTE_H
#define ZX_DAMAGETINT_COMPUTE_H

namespace zx { namespace damagetint {

// Intensity ramp: rise toward 1 while standing in the damage, fall toward 0 after stepping out.
// dtTics is whole game tics elapsed since the last update (clamped inside); riseTics/fallTics are
// the full 0->1 / 1->0 durations. Pure lerp-by-time, clamped to [0,1].
float RampStep(float current, bool active, int dtTics, float riseTics, float fallTics);

// Damage applies on the 32-tic cycle (level.time & 0x1f == 0). The tint throbs with it: a spike
// right after each cycle boundary, decaying back to 1.0 over `decayTics`. ticsIntoCycle is
// (level.time & 31). Returns a multiplier in [1.0, 1.0+peak].
float PulseFactor(int ticsIntoCycle, float peak, int decayTics);

// Final blend percentage: base cvar strength scaled by ramp intensity and pulse, clamped 0..100.
int EffectivePct(int basePct, float intensity, float pulse);

// One channel of the tint: blend from pure white (255) toward the floor color's channel by pct.
// The result multiplies the sprite texture, so 255 = untouched.
int TintChannel(int avgChannel, int pct);

// How far the gradient climbs: coverage percent of a span (actor height, weapon quad height).
// 0 -> 0 (feature off via coverage), 50 -> half the span, 100 -> the full span. Clamped.
float CoverageSpan(int coveragePct, float span);

// Strength at a given height for a sliced (multiplicative) gradient: full basePct at the bottom,
// linearly fading to 0 at coverageFrac of the span, 0 above it. fracFromBottom and coverageFrac
// are 0..1 of the same span. Used for the weapon slices and the face bands.
int SliceTintPct(int basePct, float fracFromBottom, float coverageFrac);

}} // namespace zx::damagetint

#endif
