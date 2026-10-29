// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Where V_BreakLines breaks a text into lines no wider than a width, found without making a string, so only the lines kept are built.
#ifndef ZX_LINEBREAK_COMPUTE_H
#define ZX_LINEBREAK_COMPUTE_H
#include <cstddef>
namespace zx { namespace textlayout {
// [Doruk-Aksoy] The byte that starts a colour code, as v_text.h's TEXTCOLOR_ESCAPE.
const unsigned char kColorEscape = '\034';
// [Doruk-Aksoy] One line: the text from start to stop, opened by the colour code of colorLen bytes at color (none when 0).
struct LineBreak
{
	const unsigned char *start, *stop;
	const unsigned char *color;
	size_t colorLen;
};
// [Doruk-Aksoy] A glyph's width in pixels, asked of the font through ctx.
typedef int (*CharWidthFn)(void *ctx, int c);
// [Doruk-Aksoy] Breaks text into at most maxLines lines no wider than maxwidth, as V_BreakLines always has, and returns how many it wrote to out.
int ComputeLineBreaks(const unsigned char *text, int maxwidth, bool preservecolor, int kerning, CharWidthFn charWidth, void *ctx,
	LineBreak *out, int maxLines);
}} // namespace zx::textlayout
#endif // ZX_LINEBREAK_COMPUTE_H
