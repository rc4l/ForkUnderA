// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_TRUECOLORFONT_COMPUTE_H
#define ZX_TRUECOLORFONT_COMPUTE_H

#include <cstdint>

namespace zx { namespace truecolorfont {

// [Doruk-Aksoy] One step of a TEXTCOLO colour: brightness levels RangeStart..RangeEnd (of 0..256) shade from Start to End; a colour's steps rise, and a step starting at or below the last one's end begins the next colour.
struct TranslationParm
{
	short RangeStart;
	short RangeEnd;
	uint8_t Start[3];
	uint8_t End[3];
};

// [Doruk-Aksoy] Where a glyph's pixels come from, as a font decides whether it keeps its glyphs' own colours.
enum GlyphSource
{
	kGlyphAbsent,
	kGlyphPaletted,
	kGlyphTrueColor,
};

// [Doruk-Aksoy] Which byte of a four-byte texel is red: a texture's buffer is RGBA, an FBitmap BGRA.
enum PixelOrder
{
	kRGBA,
	kBGRA,
};

// [Doruk-Aksoy] A colour's brightness, 0 to 255, weighed as UZDoom's fonts weigh it.
int ComputeLuminance(int r, int g, int b);

// [Doruk-Aksoy] The colour, as opaque 0xAARRGGBB, that brightness level v gets in the text colour whose steps start at parms.
uint32_t ComputeRangeColor(const TranslationParm *parms, int v);

// [Doruk-Aksoy] The first step of the text colour after the one whose steps start at parms.
const TranslationParm *ComputeNextColorParms(const TranslationParm *parms);

// [Doruk-Aksoy] The text colour's 256 brightness levels, as UZDoom's CreateLuminosityTranslationRanges lays out one colour.
void ComputeLuminosityRamp(const TranslationParm *parms, uint32_t ramp[256]);

// [Doruk-Aksoy] Widens [minlum, maxlum] to every texel of an image that is not fully transparent, as UZDoom's RecordLuminosity does.
void ComputeLuminosityRange(const uint8_t *pixels, int count, PixelOrder order, int &minlum, int &maxlum);

// [Doruk-Aksoy] What UZDoom's V_ApplyLuminosityTranslation turns a texel of each brightness into, for a font whose texels range minlum..maxlum.
void ComputeLuminosityTable(const uint32_t ramp[256], int minlum, int maxlum, uint32_t table[256]);

// [Doruk-Aksoy] Recolours each texel to the table entry of its brightness, keeping its alpha.
void ComputeLuminanceRecolor(uint8_t *pixels, int count, PixelOrder order, const uint32_t table[256]);

// [Doruk-Aksoy] Whether a font draws its glyphs' own pixels: it has a glyph, and every glyph it has is truecolor.
bool ComputeFontKeepsTrueColor(const GlyphSource *glyphs, int count);

}} // namespace zx::truecolorfont

#endif // ZX_TRUECOLORFONT_COMPUTE_H
