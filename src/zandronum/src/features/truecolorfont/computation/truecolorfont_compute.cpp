// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/truecolorfont/computation/truecolorfont_compute.h"

#include <cstdint>

namespace zx { namespace truecolorfont {

namespace {

uint32_t Argb(int r, int g, int b)
{
	return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

int Clamp255(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : v;
}

// [Doruk-Aksoy] Where red sits in a texel of this order; blue is across from it.
int RedAt(PixelOrder order) { return order == kRGBA ? 0 : 2; }

int Red(uint32_t c) { return (c >> 16) & 255; }
int Green(uint32_t c) { return (c >> 8) & 255; }
int Blue(uint32_t c) { return c & 255; }

} // namespace

int ComputeLuminance(int r, int g, int b)
{
	return (r * 77 + g * 143 + b * 37) >> 8;
}

uint32_t ComputeRangeColor(const TranslationParm *parms, int v)
{
	// [Doruk-Aksoy] The step v lies in, or the colour's last step when none holds it.
	while (!(parms->RangeStart <= v && parms->RangeEnd >= v) && parms[1].RangeStart > parms[0].RangeEnd)
	{
		parms++;
	}
	// [Doruk-Aksoy] Multiplied rather than shifted, since v can lie below the last step's start and a negative left shift is undefined.
	const int rangev = (v - parms->RangeStart) * 256 / (parms->RangeEnd - parms->RangeStart);
	int rgb[3];
	for (int i = 0; i < 3; i++)
	{
		rgb[i] = Clamp255(((parms->Start[i] << 8) + rangev * (parms->End[i] - parms->Start[i])) >> 8);
	}
	return Argb(rgb[0], rgb[1], rgb[2]);
}

const TranslationParm *ComputeNextColorParms(const TranslationParm *parms)
{
	while (parms[1].RangeStart > parms[0].RangeEnd)
	{
		parms++;
	}
	return parms + 1;
}

void ComputeLuminosityRamp(const TranslationParm *parms, uint32_t ramp[256])
{
	for (int v = 0; v < 256; v++)
	{
		ramp[v] = ComputeRangeColor(parms, v);
	}
}

void ComputeLuminosityRange(const uint8_t *pixels, int count, PixelOrder order, int &minlum, int &maxlum)
{
	const int r = RedAt(order);
	for (int i = 0; i < count; i++, pixels += 4)
	{
		if (pixels[3] == 0) continue;
		const int lum = ComputeLuminance(pixels[r], pixels[1], pixels[2 - r]);
		if (lum < minlum) minlum = lum;
		if (lum > maxlum) maxlum = lum;
	}
}

void ComputeLuminosityTable(const uint32_t ramp[256], int minlum, int maxlum, uint32_t table[256])
{
	// [Doruk-Aksoy] A font with no opaque texel has no range, so it takes the whole one rather than divide by a range that is empty.
	if (maxlum < minlum)
	{
		minlum = 0;
		maxlum = 255;
	}
	const int range = maxlum - minlum + 1;
	for (int gray = 0; gray < 256; gray++)
	{
		const uint32_t c = ramp[Clamp255((gray - minlum) * 255 / range)];
		int r = Red(c), g = Green(c), b = Blue(c);
		// [Doruk-Aksoy] A texel outside the font's range darkens or brightens its end of the ramp in proportion, as UZDoom extends it.
		if (gray < minlum)
		{
			r = r * gray / minlum;
			g = g * gray / minlum;
			b = b * gray / minlum;
		}
		else if (gray > maxlum && maxlum != 0)
		{
			r = Clamp255(r * gray / maxlum);
			g = Clamp255(g * gray / maxlum);
			b = Clamp255(b * gray / maxlum);
		}
		table[gray] = Argb(r, g, b);
	}
}

void ComputeLuminanceRecolor(uint8_t *pixels, int count, PixelOrder order, const uint32_t table[256])
{
	const int r = RedAt(order);
	// [Doruk-Aksoy] Transparent texels too, so a filtered glyph's edges blend toward its new colour rather than its old one.
	for (int i = 0; i < count; i++, pixels += 4)
	{
		const uint32_t c = table[ComputeLuminance(pixels[r], pixels[1], pixels[2 - r])];
		pixels[r] = uint8_t(Red(c));
		pixels[1] = uint8_t(Green(c));
		pixels[2 - r] = uint8_t(Blue(c));
	}
}

bool ComputeFontKeepsTrueColor(const GlyphSource *glyphs, int count)
{
	bool any = false;
	for (int i = 0; i < count; i++)
	{
		if (glyphs[i] == kGlyphPaletted) return false;
		if (glyphs[i] == kGlyphTrueColor) any = true;
	}
	return any;
}

}} // namespace zx::truecolorfont
