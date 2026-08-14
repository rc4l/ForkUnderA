// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/colortext_compute.h"

using zx::ComputeColorSafeCutPoints;
using zx::StripColorCodes;
using zx::kColorEscape;
using std::size_t;
using std::string;
using std::vector;

namespace
{
bool Has(const vector<size_t> &v, size_t n)
{
	for (size_t i = 0; i < v.size(); ++i)
		if (v[i] == n)
			return true;
	return false;
}
const string ESC(1, kColorEscape);
} // namespace

TEST(ColorSafeCutPoints, APlainStringCanBeCutAnywhere)
{
	const vector<size_t> got = ComputeColorSafeCutPoints("abcd");
	ASSERT_EQ(5u, got.size());
	for (size_t i = 0; i <= 4; ++i)
		EXPECT_TRUE(Has(got, i)) << "missing " << i;
}

TEST(ColorSafeCutPoints, NeverCutsBetweenAnEscapeAndItsColourCharacter)
{
	// "\x1Cd" + "Hi": offset 1 sits between the escape and the 'd', which would leave a dangling
	// escape that eats the next glyph.
	const vector<size_t> got = ComputeColorSafeCutPoints(ESC + "dHi");
	EXPECT_FALSE(Has(got, 1)) << "offered a cut inside the colour code";
	EXPECT_TRUE(Has(got, 0));
	EXPECT_TRUE(Has(got, 2));		// just past the code
	EXPECT_TRUE(Has(got, 3));
	EXPECT_TRUE(Has(got, 4));
}

TEST(ColorSafeCutPoints, TreatsABracketedNameAsOneUnit)
{
	// "\x1C[Red]X" -- every offset inside the brackets is unsafe.
	const string s = ESC + "[Red]X";
	const vector<size_t> got = ComputeColorSafeCutPoints(s);
	for (size_t i = 1; i <= 5; ++i)
		EXPECT_FALSE(Has(got, i)) << "offered a cut inside the bracketed code at " << i;
	EXPECT_TRUE(Has(got, 6));		// past "]"
	EXPECT_TRUE(Has(got, 7));		// past "X"
}

TEST(ColorSafeCutPoints, HandlesSeveralCodesInOneName)
{
	// The real case from the request: "\cdColourful \chServer" after colorizing.
	const string s = ESC + "dColourful " + ESC + "hServer";
	const vector<size_t> got = ComputeColorSafeCutPoints(s);
	EXPECT_FALSE(Has(got, 1));					// inside the first code
	EXPECT_TRUE(Has(got, 2));					// start of "Colourful"
	const size_t second = 2 + string("Colourful ").size();
	EXPECT_TRUE(Has(got, second));				// just before the second escape
	EXPECT_FALSE(Has(got, second + 1));			// inside the second code
	EXPECT_TRUE(Has(got, second + 2));			// start of "Server"
	EXPECT_TRUE(Has(got, s.size()));
}

TEST(ColorSafeCutPoints, AlwaysOffersTheWholeStringAndNothing)
{
	const string s = ESC + "dabc";
	const vector<size_t> got = ComputeColorSafeCutPoints(s);
	EXPECT_TRUE(Has(got, 0));
	EXPECT_TRUE(Has(got, s.size()));
}

TEST(ColorSafeCutPoints, AnEmptyStringHasOnlyTheEmptyCut)
{
	const vector<size_t> got = ComputeColorSafeCutPoints("");
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ(0u, got[0]);
}

TEST(ColorSafeCutPoints, SurvivesAnEscapeAtTheVeryEnd)
{
	// A truncated name can end on a bare escape; the cut list must not run past the buffer.
	const vector<size_t> got = ComputeColorSafeCutPoints("ab" + ESC);
	EXPECT_TRUE(Has(got, 3));
	EXPECT_EQ(3u, got.back());
}

TEST(ColorSafeCutPoints, SurvivesAnUnterminatedBracket)
{
	// The renderer consumes to the end of the string; so must we, or we would offer a cut the
	// renderer treats as still inside the code.
	const string s = ESC + "[Unclosed";
	const vector<size_t> got = ComputeColorSafeCutPoints(s);
	EXPECT_EQ(s.size(), got.back());
	for (size_t i = 1; i < s.size(); ++i)
		EXPECT_FALSE(Has(got, i)) << "offered a cut inside the unterminated code at " << i;
}

TEST(ColorSafeCutPoints, CutPointsAreAscendingAndUnique)
{
	const string s = ESC + "dA" + ESC + "[Gold]B" + ESC + "-C";
	const vector<size_t> got = ComputeColorSafeCutPoints(s);
	for (size_t i = 1; i < got.size(); ++i)
		EXPECT_GT(got[i], got[i - 1]) << "not ascending at " << i;
	EXPECT_EQ(s.size(), got.back());
}

// ---------------------------------------------------------------- stripping

TEST(StripColorCodes, LeavesPlainTextAlone)
{
	EXPECT_EQ("Bob", StripColorCodes("Bob"));
	EXPECT_EQ("", StripColorCodes(""));
}

TEST(StripColorCodes, RemovesTheSingleCharacterForm)
{
	EXPECT_EQ("Red", StripColorCodes(ESC + "gRed"));
	EXPECT_EQ("AB", StripColorCodes(ESC + "dA" + ESC + "hB"));
}

TEST(StripColorCodes, RemovesTheBracketedForm)
{
	// [rc4l] THE FORM THIS EXISTS FOR. A palette mod's colour is named, not lettered, and the name is
	// meaningless to a client without that mod -- so the whole code goes, brackets and all.
	EXPECT_EQ("Fancy", StripColorCodes(ESC + "[Rainbow]Fancy"));
	EXPECT_EQ("AB", StripColorCodes("A" + ESC + "[Some Long Name]B"));
}

TEST(StripColorCodes, DoesNotEatTheLetterAfterABracketedCode)
{
	// The off-by-one that a naive "skip to ]" gets wrong, and it is invisible in short names: the
	// bracket's own closing byte is part of the code, the next byte is the name.
	EXPECT_EQ("x", StripColorCodes(ESC + "[Gold]x"));
}

TEST(StripColorCodes, DropsATrailingEscapeRatherThanKeepingIt)
{
	// A name cut by a length limit upstream can end mid-escape. Keeping the stray byte would draw it
	// as a glyph in a list that is supposed to be plain text.
	EXPECT_EQ("Bob", StripColorCodes("Bob" + ESC));
	EXPECT_EQ("Bob", StripColorCodes("Bob" + ESC + "["));
}

TEST(StripColorCodes, RunsToTheEndOfAnUnterminatedBracket)
{
	// Matching ComputeColorSafeCutPoints and the renderer: an unclosed code swallows the rest.
	EXPECT_EQ("", StripColorCodes(ESC + "[Unclosed"));
}

TEST(StripColorCodes, AgreesWithTheCutPointWalk)
{
	// [rc4l] The two walks must classify bytes identically -- they are the same parse written twice.
	// Every byte the stripper keeps has to be one the cutter would let you cut just after.
	const string s = ESC + "dA" + ESC + "[Gold]B" + ESC + "-C" + ESC;
	EXPECT_EQ("ABC", StripColorCodes(s));
}
