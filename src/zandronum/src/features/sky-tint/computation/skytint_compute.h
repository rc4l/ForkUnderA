// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Deriving an outdoor light colour from a sky texture.
//
// The honest framing: what a renderer actually wants from a sky is IRRADIANCE, which depends on the
// surface normal, and the industry answer is spherical harmonics (L2, nine coefficients) or an
// ambient cube. A single averaged colour is the L0 term of that expansion -- the zeroth order.
//
// We take the zeroth order on purpose, because Doom's lighting model cannot express anything more:
// a sector carries ONE light colour, applied to floor, ceiling, walls and sprites alike, with no
// normals anywhere. Given that ceiling, the useful work is not a better basis, it is choosing which
// pixels to average and how:
//
//   - average in LINEAR light, not in sRGB bytes. Summing gamma-encoded values is the wrong mean
//     and biases dark; this is a correctness fix, not a taste one.
//   - a mean smears a high-contrast sky (bright fire, dark smoke) into a muddy colour that matches
//     neither half, so a dominant-colour mode is offered as well.
//   - weight rows either by what the player SEES (the horizon band) or by what actually lights a
//     floor (the cosine term, which favours the zenith). Those two disagree; the caller picks.
//   - clamp saturation, so a violently coloured sky cannot turn the whole level into a filter.
//
// All of it is pure so the arithmetic can be tested without a renderer, a level, or a GPU.

#ifndef ZX_SKYTINT_COMPUTE_H
#define ZX_SKYTINT_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx
{

// 8-bit sRGB, the form pixels arrive in and tints leave in.
struct SkyRgb
{
	int r, g, b;

	SkyRgb();
	SkyRgb(int r, int g, int b);
	bool operator==(const SkyRgb &other) const;
};

// How to reduce many pixels to one colour.
enum class SkyAverage
{
	Mean = 0,		// linear-light mean: faithful, can be muddy on a two-tone sky
	Dominant = 1,	// heaviest bucket of a coarse histogram: the colour a person would name
};

// Which part of the sky counts, and how much.
enum class SkyWeight
{
	Horizon = 0,	// the band the player looks at; matches the perceived mood of the map
	Cosine = 1,		// cosine of elevation: what actually irradiates a horizontal surface
};

// sRGB byte <-> linear light. The whole reason the averaging is done twice over.
double LinearFromSrgb(int byte);
int SrgbFromLinear(double linear);

// Weight for one row of the sky texture, row 0 at the top. Never negative.
double RowWeight(int row, int height, SkyWeight mode);

// Reduce a sky image to one colour. `pixels` is row-major, `width` columns per row.
//
// Averaging happens in linear light and the result is re-encoded, so the answer is the colour of
// the light rather than the average of its encoding.
SkyRgb AverageSky(const std::vector<SkyRgb> &pixels, int width, SkyAverage mode, SkyWeight weight);

// Scale so the largest component is 255: keeps hue, discards brightness. The sky says what colour
// the light is; the map's own light levels say how much of it there is.
SkyRgb NormaliseBrightness(SkyRgb colour);

// Pull toward grey until saturation is at most `maxPct` (0 = grey, 100 = untouched).
SkyRgb ClampSaturation(SkyRgb colour, int maxPct);

// Saturation as a percentage, by the HSV definition: (max - min) / max.
int SaturationPct(SkyRgb colour);

// White blended toward `tint` by `pct`. pct 0 leaves white, so the caller can fade the effect out
// without special-casing "off".
SkyRgb BlendFromWhite(SkyRgb tint, int pct);

// Strength at `hop` steps from an open-sky sector, halving each step. Past `maxHops` there is none.
int StrengthAtHop(int pct, int hop, int maxHops);

} // namespace zx

#endif
