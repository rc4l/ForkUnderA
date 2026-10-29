// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/text-layout/computation/linebreak_compute.h"

#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <vector>

using namespace zx::textlayout;

namespace {

// [Doruk-Aksoy] Widths that differ glyph to glyph, as a real font's do.
int FakeWidth(void *, int c)
{
	return c == '\n' ? 0 : 3 + (c * 7) % 11;
}

int FakeWidthPlain(int c) { return FakeWidth(nullptr, c); }

// [Doruk-Aksoy] V_BreakLines as it was before its lines were found as places in the text: each line's whole string, colour code first.
std::vector<std::string> ReferenceBreak(const unsigned char *string, int maxwidth, bool preservecolor, int kerning, size_t maxLines)
{
	std::vector<std::string> lines;
	const unsigned char *space = NULL, *start = string;
	int c, w = 0, nw;
	std::string lastcolor, linecolor;
	bool lastWasSpace = false;
	auto breakit = [&](const unsigned char *from, const unsigned char *to) {
		std::string text;
		if (!linecolor.empty()) { text += char(kColorEscape); text += linecolor; }
		text.append((const char *)from, to - from);
		lines.push_back(text);
	};
	while ((c = *string++) && lines.size() < maxLines)
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
					lastcolor.assign((const char *)code, string - code);
				}
				else lastcolor = std::string(1, (char)*string++);
			}
			continue;
		}
		if (isspace(c)) { if (!lastWasSpace) { space = string - 1; lastWasSpace = true; } }
		else lastWasSpace = false;
		nw = FakeWidthPlain(c);
		if ((w > 0 && w + nw > maxwidth) || c == '\n')
		{
			if (!space) space = string - 1;
			breakit(start, space);
			if (c == '\n' && !preservecolor) lastcolor = "";
			linecolor = lastcolor;
			w = 0;
			lastWasSpace = false;
			start = space;
			space = NULL;
			while (*start && isspace(*start) && *start != '\n') start++;
			if (*start == '\n') start++;
			else while (*start && isspace(*start)) start++;
			string = start;
		}
		else w += nw + kerning;
	}
	if (lines.size() < maxLines && --string - start >= 1)
	{
		const unsigned char *s = start;
		while (s < string) if (!isspace(*s++)) { breakit(start, string); break; }
	}
	return lines;
}

// [Doruk-Aksoy] The lines ComputeLineBreaks found, each as the string V_BreakLines builds from it.
std::vector<std::string> Broken(const std::string &text, int maxwidth, bool preservecolor, int kerning, int maxLines)
{
	std::vector<LineBreak> found(maxLines > 0 ? maxLines : 1);
	const int n = ComputeLineBreaks((const unsigned char *)text.c_str(), maxwidth, preservecolor, kerning, &FakeWidth, nullptr, found.data(), maxLines);
	std::vector<std::string> lines;
	for (int i = 0; i < n; i++)
	{
		std::string line;
		if (found[i].colorLen != 0) { line += char(kColorEscape); line.append((const char *)found[i].color, found[i].colorLen); }
		line.append((const char *)found[i].start, found[i].stop - found[i].start);
		lines.push_back(line);
	}
	return lines;
}

void ExpectSame(const std::string &text, int maxwidth, bool preservecolor, int kerning = 0, int maxLines = 128)
{
	EXPECT_EQ(Broken(text, maxwidth, preservecolor, kerning, maxLines),
		ReferenceBreak((const unsigned char *)text.c_str(), maxwidth, preservecolor, kerning, (size_t)maxLines))
		<< "text \"" << text << "\" width " << maxwidth << (preservecolor ? " preserving colour" : "");
}

const std::string kRed = std::string(1, char(kColorEscape)) + "[Red]";
const std::string kGold = std::string(1, char(kColorEscape)) + "f";

} // namespace

// [Doruk-Aksoy] Words wrap at the last space before the width runs out, and the spaces at a break are dropped.
TEST(LineBreakCompute, WordsWrapAtSpaces)
{
	const std::vector<std::string> lines = Broken("one two three four", 40, false, 0, 128);
	ASSERT_GE(lines.size(), 2u);
	EXPECT_EQ(lines[0].find(' '), std::string::npos) << "a break never keeps the space it broke at";
	ExpectSame("one two three four", 40, false);
	ExpectSame("one two three four", 1000, false);
}

// [Doruk-Aksoy] A word wider than the width is cut where it runs out, and a newline always breaks.
TEST(LineBreakCompute, LongWordsAndNewlines)
{
	ExpectSame("supercalifragilistic", 30, false);
	ExpectSame("a\nb\n\nc", 1000, false);
	ExpectSame("trailing spaces    ", 20, false);
	ExpectSame("   leading", 20, false);
	ExpectSame("\n", 50, false);
}

// [Doruk-Aksoy] A colour code opens the next line, and a newline drops it unless the console asks to keep it.
TEST(LineBreakCompute, ColourCodesCarryAcrossBreaks)
{
	const std::string text = kRed + "warm words wrap around\nnext " + kGold + "gold line here";
	for (bool keep : { false, true })
		for (int width : { 25, 40, 1000 }) ExpectSame(text, width, keep);
	const std::vector<std::string> kept = Broken(kRed + "a\nb", 1000, true, 0, 128);
	ASSERT_EQ(kept.size(), 2u);
	EXPECT_EQ(kept[1], kRed + "b") << "the console keeps the colour across a newline";
	const std::vector<std::string> plain = Broken(kRed + "a\nb", 1000, false, 0, 128);
	ASSERT_EQ(plain.size(), 2u);
	EXPECT_EQ(plain[1], "b") << "anything else starts the line after a newline plain";
}

// [Doruk-Aksoy] Codes left open or at the very end, and kerning, break the same way as before.
TEST(LineBreakCompute, OddCodesAndKerning)
{
	ExpectSame(std::string(1, char(kColorEscape)), 50, false);
	ExpectSame("end" + std::string(1, char(kColorEscape)), 50, false);
	ExpectSame(std::string(1, char(kColorEscape)) + "[Unclosed text here", 30, false);
	ExpectSame("kerned words wrap sooner", 50, false, 4);
	ExpectSame("kerned words wrap later", 50, false, -2);
}

// [Doruk-Aksoy] Nothing, or only white space, makes no line.
TEST(LineBreakCompute, EmptyTextHasNoLines)
{
	EXPECT_TRUE(Broken("", 100, false, 0, 128).empty());
	EXPECT_TRUE(Broken("   \t ", 100, false, 0, 128).empty());
	ExpectSame("", 100, false);
	ExpectSame(kRed, 100, false);
}

// [Doruk-Aksoy] No more lines are made than there is room for.
TEST(LineBreakCompute, StopsAtTheLineLimit)
{
	std::string text;
	for (int i = 0; i < 300; i++) text += "x\n";
	EXPECT_EQ(Broken(text, 100, false, 0, 128).size(), 128u);
	ExpectSame(text, 100, false);
	ExpectSame(text, 100, false, 0, 3);
	EXPECT_TRUE(Broken("abc", 100, false, 0, 0).empty());
}

// [Doruk-Aksoy] Thousands of generated texts of words, spaces, tabs, newlines and colour codes break exactly as before.
TEST(LineBreakCompute, MatchesTheOldBreakerOnGeneratedTexts)
{
	unsigned seed = 12345;
	auto next = [&seed](unsigned n) { seed = seed * 1103515245u + 12345u; return (seed >> 16) % n; };
	const char *pieces[] = { "a", "be", "cat", "dove", "eagle", " ", "  ", "\t", "\n", "-", "WWW", "i" };
	for (int t = 0; t < 20000; t++)
	{
		std::string text;
		const unsigned parts = next(30);
		for (unsigned p = 0; p < parts; p++)
		{
			switch (next(10))
			{
			case 0: text += kRed; break;
			case 1: text += std::string(1, char(kColorEscape)) + char('a' + next(20)); break;
			default: text += pieces[next(sizeof(pieces) / sizeof(pieces[0]))]; break;
			}
		}
		ExpectSame(text, 5 + (int)next(80), next(2) == 1, (int)next(4) - 1, next(5) == 0 ? 2 : 128);
		if (HasFailure()) return;
	}
}
