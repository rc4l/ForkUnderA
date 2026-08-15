// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] See skytint_compute.h for why this is the zeroth order of sky lighting on purpose.

#include "features/sky-tint/computation/skytint_compute.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace zx
{

namespace
{

int Clamp255(int v)
{
	if (v < 0)		return 0;
	if (v > 255)	return 255;

	return v;
}

// [rc4l] The dominant-colour histogram is coarse on purpose. Fine buckets split one visual colour
// across neighbours and the "heaviest bucket" stops meaning anything; 32 levels per channel is
// enough to separate fire from smoke and not enough to separate fire from slightly-different fire.
const int kBucketShift = 3;			// 256 >> 3 = 32 levels per channel

int BucketOf(const SkyRgb &c)
{
	return (((c.r >> kBucketShift) * 32) + (c.g >> kBucketShift)) * 32 + (c.b >> kBucketShift);
}

} // namespace

SkyRgb::SkyRgb()
	: r(0), g(0), b(0)
{
}

SkyRgb::SkyRgb(int r, int g, int b)
	: r(Clamp255(r)), g(Clamp255(g)), b(Clamp255(b))
{
}

bool SkyRgb::operator==(const SkyRgb &other) const
{
	return (r == other.r) && (g == other.g) && (b == other.b);
}

double LinearFromSrgb(int byte)
{
	const double s = Clamp255(byte) / 255.0;

	// The standard sRGB transfer function, including its linear toe near black. The toe matters
	// here: a sky is mostly dark pixels, and approximating it with a pure 2.2 power pulls the
	// average further down than it belongs.
	return (s <= 0.04045) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
}

int SrgbFromLinear(double linear)
{
	if (linear <= 0.0)
		return 0;
	if (linear >= 1.0)
		return 255;

	const double s = (linear <= 0.0031308)
		? (linear * 12.92)
		: (1.055 * std::pow(linear, 1.0 / 2.4) - 0.055);

	return Clamp255(static_cast<int>((s * 255.0) + 0.5));
}

int RowsAboveHorizon(int height)
{
	if (height <= 0)
		return 0;

	// See the header: gl_skydome scales anything taller by 240/height, so the upper hemisphere ends
	// at row 240 and the rest of the texture is drawn below eye level.
	return (height > 240) ? 240 : height;
}

double RowWeight(int row, int height, SkyWeight mode)
{
	if (height <= 0)
		return 0.0;
	if (row < 0)
		row = 0;
	if (row >= height)
		row = height - 1;

	// Below the horizon contributes nothing. It cannot light a floor, and it is not what a player
	// looking around actually sees, so it has no business in either weighting.
	const int visible = RowsAboveHorizon(height);
	if (row >= visible)
		return 0.0;

	if (mode == SkyWeight::Horizon)
	{
		// The lower half OF THE VISIBLE BAND. Not the lower half of the texture: on a tall sky those
		// are different by a long way, and the texture-half version was mostly below the horizon.
		return (row >= (visible / 2)) ? 1.0 : 0.0;
	}

	// Cosine of elevation across the visible band. Row 0 is the zenith and the last visible row is
	// the horizon, so elevation runs 90 degrees down to 0. A horizontal surface takes light in
	// proportion to cos(angle-from-normal), which is largest straight overhead.
	const double t = (visible > 1) ? (static_cast<double>(row) / static_cast<double>(visible - 1)) : 0.0;
	const double elevation = (1.0 - t) * (3.14159265358979323846 / 2.0);

	return std::sin(elevation);		// sin(elevation) == cos(angle from straight up)
}

SkyRgb AverageSky(const std::vector<SkyRgb> &pixels, int width, SkyAverage mode, SkyWeight weight)
{
	if (pixels.empty() || (width <= 0))
		return SkyRgb(255, 255, 255);

	const int height = static_cast<int>(pixels.size()) / width;
	if (height <= 0)
		return SkyRgb(255, 255, 255);

	if (mode == SkyAverage::Dominant)
	{
		// Heaviest bucket wins, weighted the same way the mean would be. Ties break on the lower
		// bucket index so the answer does not wander between runs.
		std::map<int, double> weightOf;
		std::map<int, std::size_t> firstAt;

		for (int y = 0; y < height; ++y)
		{
			const double w = RowWeight(y, height, weight);
			if (w <= 0.0)
				continue;

			for (int x = 0; x < width; ++x)
			{
				const std::size_t at = (static_cast<std::size_t>(y) * width) + x;
				const int bucket = BucketOf(pixels[at]);

				weightOf[bucket] += w;
				if (firstAt.find(bucket) == firstAt.end())
					firstAt[bucket] = at;
			}
		}

		if (weightOf.empty())
			return SkyRgb(255, 255, 255);

		int best = weightOf.begin()->first;
		for (std::map<int, double>::const_iterator it = weightOf.begin(); it != weightOf.end(); ++it)
		{
			if (it->second > weightOf[best])
				best = it->first;
		}

		// Average the members of the winning bucket rather than returning the bucket centre, so the
		// answer is a colour that was actually in the sky.
		double lr = 0.0, lg = 0.0, lb = 0.0, total = 0.0;
		for (int y = 0; y < height; ++y)
		{
			const double w = RowWeight(y, height, weight);
			if (w <= 0.0)
				continue;

			for (int x = 0; x < width; ++x)
			{
				const SkyRgb &p = pixels[(static_cast<std::size_t>(y) * width) + x];
				if (BucketOf(p) != best)
					continue;

				lr += LinearFromSrgb(p.r) * w;
				lg += LinearFromSrgb(p.g) * w;
				lb += LinearFromSrgb(p.b) * w;
				total += w;
			}
		}

		if (total <= 0.0)
			return SkyRgb(255, 255, 255);

		return SkyRgb(SrgbFromLinear(lr / total), SrgbFromLinear(lg / total), SrgbFromLinear(lb / total));
	}

	// [rc4l] Mean, in LINEAR light. Summing sRGB bytes averages the encoding rather than the light
	// and lands too dark, which is the bug this whole file exists to not repeat.
	double lr = 0.0, lg = 0.0, lb = 0.0, total = 0.0;

	for (int y = 0; y < height; ++y)
	{
		const double w = RowWeight(y, height, weight);
		if (w <= 0.0)
			continue;

		for (int x = 0; x < width; ++x)
		{
			const SkyRgb &p = pixels[(static_cast<std::size_t>(y) * width) + x];

			lr += LinearFromSrgb(p.r) * w;
			lg += LinearFromSrgb(p.g) * w;
			lb += LinearFromSrgb(p.b) * w;
			total += w;
		}
	}

	if (total <= 0.0)
		return SkyRgb(255, 255, 255);

	return SkyRgb(SrgbFromLinear(lr / total), SrgbFromLinear(lg / total), SrgbFromLinear(lb / total));
}

SkyRgb NormaliseBrightness(SkyRgb colour)
{
	const int maxv = std::max(colour.r, std::max(colour.g, colour.b));
	if (maxv <= 0)
		return SkyRgb(255, 255, 255);		// a black sky has no hue to offer

	return SkyRgb((colour.r * 255) / maxv, (colour.g * 255) / maxv, (colour.b * 255) / maxv);
}

double SkyLuminance(SkyRgb colour)
{
	// Rec. 709 weights on LINEARISED components. Green carries most of what an eye reads as
	// brightness, which matters here precisely because the skies that cause trouble are green ones.
	return (0.2126 * LinearFromSrgb(colour.r))
		+ (0.7152 * LinearFromSrgb(colour.g))
		+ (0.0722 * LinearFromSrgb(colour.b));
}

int StrengthForSky(int pct, double luminance, int respectPct)
{
	if (respectPct <= 0)
		return pct;			// hue only, whatever the sky's brightness

	if (respectPct > 100)
		respectPct = 100;
	if (luminance < 0.0)
		luminance = 0.0;
	if (luminance > 1.0)
		luminance = 1.0;

	// Blend between "ignore the sky's brightness" and "let it decide", so the control is a dial
	// rather than a switch and a map with a mid-bright sky lands somewhere sensible.
	const double t = respectPct / 100.0;
	const double scale = (1.0 - t) + (t * luminance);

	return static_cast<int>((pct * scale) + 0.5);
}

int StrengthForSectorLight(int pct, int lightLevel, int respectPct)
{
	if (respectPct <= 0)
		return pct;			// every sector tinted alike, however dark it is

	if (respectPct > 100)
		respectPct = 100;
	if (lightLevel < 0)
		lightLevel = 0;
	if (lightLevel > 255)
		lightLevel = 255;

	// Doom's light level is already perceptual rather than linear -- mappers pick 128 to mean "half
	// lit", not "a fifth of the photons" -- so it is used as it stands rather than linearised. The
	// point is to follow the mapper's intent, not to be physically correct about their intent.
	const double t = respectPct / 100.0;
	const double lit = lightLevel / 255.0;
	const double scale = (1.0 - t) + (t * lit);

	return static_cast<int>((pct * scale) + 0.5);
}

int SaturationPct(SkyRgb colour)
{
	const int maxv = std::max(colour.r, std::max(colour.g, colour.b));
	const int minv = std::min(colour.r, std::min(colour.g, colour.b));

	return (maxv <= 0) ? 0 : (((maxv - minv) * 100) / maxv);
}

SkyRgb ClampSaturation(SkyRgb colour, int maxPct)
{
	if (maxPct < 0)		maxPct = 0;
	if (maxPct > 100)	maxPct = 100;

	const int have = SaturationPct(colour);
	if (have <= maxPct)
		return colour;

	// Toward the grey of equal brightness, by however much the excess demands. Pulling toward the
	// max component rather than the mean keeps the result as bright as it started, which is the
	// point of having normalised brightness away in the first place.
	const int grey = std::max(colour.r, std::max(colour.g, colour.b));
	const int keep = (have > 0) ? ((maxPct * 100) / have) : 0;

	return SkyRgb(
		((colour.r * keep) + (grey * (100 - keep))) / 100,
		((colour.g * keep) + (grey * (100 - keep))) / 100,
		((colour.b * keep) + (grey * (100 - keep))) / 100);
}

SkyRgb BlendFromWhite(SkyRgb tint, int pct)
{
	if (pct < 0)	pct = 0;
	if (pct > 100)	pct = 100;

	return SkyRgb(
		((255 * (100 - pct)) + (tint.r * pct)) / 100,
		((255 * (100 - pct)) + (tint.g * pct)) / 100,
		((255 * (100 - pct)) + (tint.b * pct)) / 100);
}

int StrengthAtDistance(int pct, double distance, double reach)
{
	if (pct <= 0)
		return 0;
	if (distance <= 0.0)
		return pct;			// the open sky itself, lit whatever the reach is set to
	if (reach <= 0.0)
		return 0;			// no reach means no light travels indoors at all
	if (distance >= reach)
		return 0;

	// Squared falloff: steep to begin with, trailing off toward the limit. Light through a doorway
	// really does die fast, and a curve avoids the visible step a per-sector halving left between
	// two pieces of the same wall.
	const double t = 1.0 - (distance / reach);

	return static_cast<int>((pct * t * t) + 0.5);
}

double OpeningFactor(double openingHeight, double fullHeight)
{
	if ((openingHeight <= 0.0) || (fullHeight <= 0.0))
		return 0.0;			// a closed door passes nothing

	const double t = openingHeight / fullHeight;

	return (t >= 1.0) ? 1.0 : t;
}

double StepCost(double distance, double openingFactor)
{
	if (distance < 0.0)
		distance = 0.0;
	if (openingFactor <= 0.0)
		return -1.0;		// impassable, and said so rather than returning a huge number

	// Floored so a hairline gap costs ten times the distance rather than infinity: a crack under a
	// door should let a little light through, just not much, and an unbounded multiplier would make
	// the result depend on floating-point noise in the opening height.
	const double factor = (openingFactor < 0.1) ? 0.1 : openingFactor;

	return distance / factor;
}

} // namespace zx
