// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/text-layout/computation/linebreak_compute.h"

#include <cctype>

namespace zx { namespace textlayout {

int ComputeLineBreaks(const unsigned char *string, int maxwidth, bool preservecolor, int kerning, CharWidthFn charWidth, void *ctx,
	LineBreak *lines, int maxLines)
{
	const unsigned char *space = NULL, *start = string;
	const unsigned char *lastcolor = NULL, *linecolor = NULL;
	size_t lastcolorLen = 0, linecolorLen = 0;
	bool lastWasSpace = false;
	int i = 0, c, w = 0, nw;

	while ((c = *string++) && i < maxLines)
	{
		if (c == kColorEscape)
		{
			if (*string)
			{
				if (*string == '[')
				{
					const unsigned char *code = string;
					while (*string != ']' && *string != '\0') string++;
					if (*string != '\0') string++;
					lastcolor = code;
					lastcolorLen = string - code;
				}
				else
				{
					lastcolor = string++;
					lastcolorLen = 1;
				}
			}
			continue;
		}

		if (isspace(c))
		{
			if (!lastWasSpace)
			{
				space = string - 1;
				lastWasSpace = true;
			}
		}
		else
		{
			lastWasSpace = false;
		}

		nw = charWidth(ctx, c);

		if ((w > 0 && w + nw > maxwidth) || c == '\n')
		{
			if (!space) space = string - 1;
			lines[i].start = start;
			lines[i].stop = space;
			lines[i].color = linecolor;
			lines[i].colorLen = linecolorLen;
			// [Doruk-Aksoy] The console re-breaks its stored lines and keeps the colour across a newline (uzdoom@9d846395b); anything else starts each line plain.
			if (c == '\n' && !preservecolor)
			{
				lastcolor = NULL;
				lastcolorLen = 0;
			}
			linecolor = lastcolor;
			linecolorLen = lastcolorLen;

			i++;
			w = 0;
			lastWasSpace = false;
			start = space;
			space = NULL;

			while (*start && isspace(*start) && *start != '\n') start++;
			if (*start == '\n') start++;
			else while (*start && isspace(*start)) start++;
			string = start;
		}
		else
		{
			w += nw + kerning;
		}
	}

	// [Doruk-Aksoy] string is one past the end's terminator here, and the remainder is a line if any of it is not white space.
	if (i < maxLines && --string - start >= 1)
	{
		for (const unsigned char *s = start; s < string; )
		{
			if (!isspace(*s++))
			{
				lines[i].start = start;
				lines[i].stop = string;
				lines[i].color = linecolor;
				lines[i].colorLen = linecolorLen;
				i++;
				break;
			}
		}
	}
	return i;
}

}} // namespace zx::textlayout
