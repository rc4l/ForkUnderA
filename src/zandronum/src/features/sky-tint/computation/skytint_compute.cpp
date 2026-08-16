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

// [rc4l] The coarse colour histogram that backed a dominant-colour mode used to live here. Removed
// with the mode; see the header for the measurements.

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

double RowWeight(int row, int height)
{
	if (height <= 0)
		return 0.0;
	if (row < 0)
		row = 0;
	if (row >= height)
		row = height - 1;

	// [rc4l] The lower half only. Not the physically correct weighting and not trying to be: it is
	// the band a player actually looks at, so it matches the mood they see.
	//
	// A cosine-of-elevation alternative was offered alongside this and has been removed. It is the
	// textbook irradiance weighting, but measured against this one it landed 0.19 and 0.21 apart out
	// of 255 on two of the three skies tested -- noise -- while being half of a combination that
	// switched the feature off entirely on a mod's maps. A knob nobody can see is not worth the
	// surface it costs.
	return (row >= (height / 2)) ? 1.0 : 0.0;
}

SkyRgb CompositeOver(SkyRgb over, int overAlpha, SkyRgb under)
{
	if (overAlpha <= 0)
		return under;
	if (overAlpha >= 255)
		return over;

	const double a = overAlpha / 255.0;
	const double r = (LinearFromSrgb(over.r) * a) + (LinearFromSrgb(under.r) * (1.0 - a));
	const double g = (LinearFromSrgb(over.g) * a) + (LinearFromSrgb(under.g) * (1.0 - a));
	const double b = (LinearFromSrgb(over.b) * a) + (LinearFromSrgb(under.b) * (1.0 - a));

	return SkyRgb(SrgbFromLinear(r), SrgbFromLinear(g), SrgbFromLinear(b));
}

std::vector<SkyRgb> CompositeSkyLayers(const std::vector<SkyRgb> &over, const std::vector<int> &overAlpha,
	int overWidth, const std::vector<SkyRgb> &under, int underWidth)
{
	std::vector<SkyRgb> out;
	if (over.empty() || (overWidth <= 0))
		return out;

	// No usable back layer: the front one is the whole picture.
	if (under.empty() || (underWidth <= 0))
		return over;

	const int overHeight = static_cast<int>(over.size()) / overWidth;
	const int underHeight = static_cast<int>(under.size()) / underWidth;
	if ((overHeight <= 0) || (underHeight <= 0))
		return over;

	out.reserve(over.size());
	for (int y = 0; y < overHeight; ++y)
	{
		// Proportional, not modulo: the layers line up by position rather than by pixel index, so
		// differing sizes scale against each other instead of sliding out of registration.
		const int uy = (overHeight == 1) ? 0 : ((y * underHeight) / overHeight);
		for (int x = 0; x < overWidth; ++x)
		{
			const int ux = (overWidth == 1) ? 0 : ((x * underWidth) / overWidth);
			const size_t oi = (static_cast<size_t>(y) * overWidth) + x;
			const size_t ui = (static_cast<size_t>(uy) * underWidth) + ux;
			if (ui >= under.size())
			{
				out.push_back(over[oi]);
				continue;
			}

			const int a = (oi < overAlpha.size()) ? overAlpha[oi] : 255;
			out.push_back(CompositeOver(over[oi], a, under[ui]));
		}
	}

	return out;
}

SkyRgb AverageSky(const std::vector<SkyRgb> &pixels, int width)
{
	if (pixels.empty() || (width <= 0))
		return SkyRgb(255, 255, 255);

	const int height = static_cast<int>(pixels.size()) / width;
	if (height <= 0)
		return SkyRgb(255, 255, 255);

	// [rc4l] Mean, in LINEAR light. Summing sRGB bytes averages the encoding rather than the light
	// and lands too dark, which is the bug this whole file exists to not repeat.
	//
	// Averaging is also what makes this robust in a way the removed dominant-colour mode was not: a
	// dark region can only pull the mean darker, never capture it outright, so a mostly-black sky
	// cannot normalise to white and switch the tint off.
	double lr = 0.0, lg = 0.0, lb = 0.0, total = 0.0;

	for (int y = 0; y < height; ++y)
	{
		const double w = RowWeight(y, height);
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

SkyRgb PreserveLuminance(SkyRgb colour)
{
	// Worked in the multiplier's own space, 0..1 per channel, because that is what the renderer will
	// multiply a surface by. Rec.709 on the multipliers directly: this is asking "how much light does
	// this filter pass", not "how bright is this colour", so the sRGB transfer does not belong here.
	const double r = colour.r / 255.0;
	const double g = colour.g / 255.0;
	const double b = colour.b / 255.0;

	const double pass = (0.2126 * r) + (0.7152 * g) + (0.0722 * b);
	if (pass <= 0.0)
		return SkyRgb(255, 255, 255);		// passes no light at all; leave the level alone

	const double scale = 1.0 / pass;

	return SkyRgb(static_cast<int>((r * scale * 255.0) + 0.5),
		static_cast<int>((g * scale * 255.0) + 0.5),
		static_cast<int>((b * scale * 255.0) + 0.5));
}

SkyLab::SkyLab()
	: L(0.0), a(0.0), b(0.0)
{
}

SkyLab::SkyLab(double L, double a, double b)
	: L(L), a(a), b(b)
{
}

namespace
{

// The CIELAB companding curve, with its linear toe near black for the same reason sRGB has one.
double LabF(double t)
{
	return (t > 0.008856) ? std::pow(t, 1.0 / 3.0) : ((7.787 * t) + (16.0 / 116.0));
}

} // namespace

SkyLab LabFromSrgb(SkyRgb colour)
{
	// sRGB -> linear -> CIEXYZ (sRGB primaries, D65) -> CIELAB.
	const double lr = LinearFromSrgb(colour.r);
	const double lg = LinearFromSrgb(colour.g);
	const double lb = LinearFromSrgb(colour.b);

	const double X = (0.4124 * lr) + (0.3576 * lg) + (0.1805 * lb);
	const double Y = (0.2126 * lr) + (0.7152 * lg) + (0.0722 * lb);
	const double Z = (0.0193 * lr) + (0.1192 * lg) + (0.9505 * lb);

	// D65 white, the reference the sRGB primaries above are defined against.
	const double fx = LabF(X / 0.95047);
	const double fy = LabF(Y / 1.00000);
	const double fz = LabF(Z / 1.08883);

	return SkyLab((116.0 * fy) - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz));
}

double DeltaE76(SkyLab x, SkyLab y)
{
	const double dL = x.L - y.L, da = x.a - y.a, db = x.b - y.b;

	return std::sqrt((dL * dL) + (da * da) + (db * db));
}

int StrengthForTargetDelta(SkyRgb scene, SkyRgb tint, double targetDelta, int maxPct)
{
	if (maxPct <= 0)
		return 0;
	if (targetDelta <= 0.0)
		return 0;

	// What the tint does to this scene at full strength. Multiply per channel, exactly as the
	// renderer will.
	const SkyRgb full(((scene.r * tint.r) / 255), ((scene.g * tint.g) / 255), ((scene.b * tint.b) / 255));
	const double reach = DeltaE76(LabFromSrgb(scene), LabFromSrgb(full));
	if (reach <= 0.0)
		return maxPct;			// this tint cannot move this scene at all; nothing to scale back

	if (reach <= targetDelta)
		return maxPct;			// even at full strength it stays under the target

	const int pct = static_cast<int>(((targetDelta / reach) * maxPct) + 0.5);
	if (pct < 1)
		return 1;				// something rather than nothing, so the feature never reads as broken

	return (pct > maxPct) ? maxPct : pct;
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

// [rc4l] See the header for the measurement this comes from and why the tax is charged evenly.
SkyRgb EqualiseHuePush(SkyRgb dir, int chromaPct)
{
	if (chromaPct <= 0)
		return SkyRgb(255, 255, 255);
	if (chromaPct > 100)
		chromaPct = 100;

	int mn = dir.r;
	if (dir.g < mn) mn = dir.g;
	if (dir.b < mn) mn = dir.b;
	int mx = dir.r;
	if (dir.g > mx) mx = dir.g;
	if (dir.b > mx) mx = dir.b;

	// A neutral sky has nothing to push in any direction, at any strength.
	const int room = mx - mn;
	if (room <= 0)
		return SkyRgb(255, 255, 255);

	// The spread being asked for, as an ABSOLUTE amount of colour rather than a fraction of this
	// particular sky. That is what makes the push equal across hues: `room` differs wildly between
	// directions (a near-pure red has ~254 of it, a pale green ~170), so blending by a fixed
	// percentage gives every hue a different result, which is the bug this exists to kill.
	const double target = (chromaPct / 100.0) * mx;

	// c = target / room. A direction whose darkest channel is near zero buys its spread almost
	// one-for-one; one sitting high has to blend much further for the same visible change.
	int pct = (int)(((target / (double)room) * 100.0) + 0.5);

	// [rc4l] Above the point where a sky's own colour runs out, it simply gives what it has.
	//
	// Green's darkest channel sits at 85, so it cannot spread past 170 however hard it is asked,
	// while a near-pure red reaches 254. Past that point the two stop matching and the ordering
	// actually inverts: measured at full dial, red came out 1.39x green. There is no fixing that
	// without inventing colour the sky does not contain, so the honest behaviour is to saturate.
	// The dial reads as "how much colour to push", equal for every sky that can reach it, and a pale
	// sky tops out early rather than being extrapolated into a hue it never had.
	if (pct > 100)
		pct = 100;

	return BlendFromWhite(dir, pct);
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
