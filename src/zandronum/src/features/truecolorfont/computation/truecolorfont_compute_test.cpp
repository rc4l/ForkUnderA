// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/truecolorfont/computation/truecolorfont_compute.h"

#include <cstdint>
#include <gtest/gtest.h>

using namespace zx::truecolorfont;

namespace {

uint32_t Argb(int r, int g, int b)
{
	return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// [Doruk-Aksoy] Two colours as V_InitFontColors stores them: A in two steps, B in one, then the terminating marker.
const TranslationParm kParms[] = {
	{ 0, 127, { 0, 0, 0 }, { 100, 0, 0 } },
	{ 128, 256, { 100, 0, 0 }, { 255, 255, 255 } },
	{ 0, 256, { 0, 0, 0 }, { 0, 0, 200 } },
	{ -1, 0, { 0, 0, 0 }, { 0, 0, 0 } },
};
const TranslationParm *const kColorA = &kParms[0];
const TranslationParm *const kColorB = &kParms[2];

// [Doruk-Aksoy] A ramp whose ends differ in every channel, so a scaled end is told apart from the other end.
void TestRamp(uint32_t ramp[256])
{
	for (int i = 0; i < 256; i++) ramp[i] = Argb(100 + i / 4, i, 255 - i);
}

} // namespace

TEST(TrueColorFont, LuminanceWeighsChannelsAsUZDoomDoes)
{
	EXPECT_EQ(ComputeLuminance(0, 0, 0), 0);
	EXPECT_EQ(ComputeLuminance(255, 255, 255), 255);
	EXPECT_EQ(ComputeLuminance(255, 0, 0), 76);
	EXPECT_EQ(ComputeLuminance(0, 255, 0), 142);
	EXPECT_EQ(ComputeLuminance(0, 0, 255), 36);
}

TEST(TrueColorFont, RangeColorInterpolatesWithinTheStepThatHoldsTheLevel)
{
	EXPECT_EQ(ComputeRangeColor(kColorA, 0), Argb(0, 0, 0));
	EXPECT_EQ(ComputeRangeColor(kColorA, 127), Argb(100, 0, 0));
	EXPECT_EQ(ComputeRangeColor(kColorA, 128), Argb(100, 0, 0));
	EXPECT_EQ(ComputeRangeColor(kColorA, 192), Argb(177, 127, 127));
	EXPECT_EQ(ComputeRangeColor(kColorA, 256), Argb(255, 255, 255));
}

TEST(TrueColorFont, RangeColorOutsideEveryStepUsesTheLastStepClamped)
{
	// [Doruk-Aksoy] Past the last step it extrapolates past white, and below it past black; each channel clamps.
	EXPECT_EQ(ComputeRangeColor(kColorA, 300), Argb(255, 255, 255));
	EXPECT_EQ(ComputeRangeColor(kColorB, -10), Argb(0, 0, 0));
}

TEST(TrueColorFont, NextColorSkipsTheStepsOfThisOne)
{
	EXPECT_EQ(ComputeNextColorParms(kColorA), kColorB);
	EXPECT_EQ(ComputeNextColorParms(kColorB), &kParms[3]);
}

TEST(TrueColorFont, RampIsEachLevelsRangeColor)
{
	uint32_t ramp[256];
	ComputeLuminosityRamp(kColorB, ramp);
	EXPECT_EQ(ramp[0], Argb(0, 0, 0));
	EXPECT_EQ(ramp[128], Argb(0, 0, 100));
	EXPECT_EQ(ramp[255], Argb(0, 0, 199));
	for (int v = 0; v < 256; v++) EXPECT_EQ(ramp[v], ComputeRangeColor(kColorB, v)) << v;
}

TEST(TrueColorFont, RangeWidensToEveryTexelThatShowsAndSkipsTransparentOnes)
{
	const uint8_t bgra[] = {
		0, 0, 255, 255,       // [Doruk-Aksoy] red, brightness 76
		255, 255, 255, 0,     // [Doruk-Aksoy] white but fully transparent
		0, 255, 0, 10,        // [Doruk-Aksoy] faint green, brightness 142
	};
	int minlum = 255, maxlum = 0;
	ComputeLuminosityRange(bgra, 3, kBGRA, minlum, maxlum);
	EXPECT_EQ(minlum, 76);
	EXPECT_EQ(maxlum, 142);

	minlum = 50; maxlum = 100;
	ComputeLuminosityRange(bgra, 3, kBGRA, minlum, maxlum);
	EXPECT_EQ(minlum, 50);
	EXPECT_EQ(maxlum, 142);
}

TEST(TrueColorFont, RangeReadsRedFirstInATexturesBuffer)
{
	// [Doruk-Aksoy] The same bytes read as RGBA are blue, brightness 36.
	const uint8_t rgba[] = { 0, 0, 255, 255 };
	int minlum = 255, maxlum = 0;
	ComputeLuminosityRange(rgba, 1, kRGBA, minlum, maxlum);
	EXPECT_EQ(minlum, 36);
	EXPECT_EQ(maxlum, 36);
}

TEST(TrueColorFont, TableStretchesTheFontsRangeOverTheRamp)
{
	uint32_t ramp[256], table[256];
	TestRamp(ramp);
	ComputeLuminosityTable(ramp, 64, 191, table);
	EXPECT_EQ(table[64], ramp[0]);
	EXPECT_EQ(table[128], ramp[127]);
	EXPECT_EQ(table[191], ramp[253]);
}

TEST(TrueColorFont, TableScalesTexelsOutsideTheFontsRange)
{
	uint32_t ramp[256], table[256];
	TestRamp(ramp);
	ComputeLuminosityTable(ramp, 64, 191, table);
	// [Doruk-Aksoy] Half the darkest brightness halves the ramp's dark end (100, 0, 255).
	EXPECT_EQ(table[32], Argb(50, 0, 127));
	// [Doruk-Aksoy] Past the brightest, the bright end (163, 255, 0) brightens by 255/191 and clamps.
	EXPECT_EQ(table[255], Argb(217, 255, 0));
}

TEST(TrueColorFont, TableOfAnAllBlackFontLeavesBrighterTexelsUnscaled)
{
	uint32_t ramp[256], table[256];
	TestRamp(ramp);
	ComputeLuminosityTable(ramp, 0, 0, table);
	EXPECT_EQ(table[0], ramp[0]);
	EXPECT_EQ(table[10], ramp[255]);
}

TEST(TrueColorFont, TableOfAFontWithNoOpaqueTexelSpansTheWholeRange)
{
	uint32_t ramp[256], empty[256], whole[256];
	TestRamp(ramp);
	ComputeLuminosityTable(ramp, 255, 0, empty);
	ComputeLuminosityTable(ramp, 0, 255, whole);
	for (int i = 0; i < 256; i++) EXPECT_EQ(empty[i], whole[i]) << i;
}

TEST(TrueColorFont, RecolorTakesEachTexelsBrightnessEntryAndKeepsItsAlpha)
{
	uint32_t table[256];
	for (int i = 0; i < 256; i++) table[i] = Argb(i, 0, 255 - i);
	uint8_t bgra[] = {
		255, 255, 255, 255,   // [Doruk-Aksoy] opaque white, brightness 255
		0, 0, 0, 0,           // [Doruk-Aksoy] transparent black, recoloured for filtering
		0, 0, 255, 128,       // [Doruk-Aksoy] half-transparent red, brightness 76
	};
	ComputeLuminanceRecolor(bgra, 3, kBGRA, table);
	const uint8_t expected[] = {
		0, 0, 255, 255,
		255, 0, 0, 0,
		179, 0, 76, 128,
	};
	for (int i = 0; i < 12; i++) EXPECT_EQ(bgra[i], expected[i]) << i;
}

TEST(TrueColorFont, RecolorWritesRedFirstInATexturesBuffer)
{
	uint32_t table[256];
	for (int i = 0; i < 256; i++) table[i] = Argb(i, 0, 255 - i);
	// [Doruk-Aksoy] Half-transparent red in RGBA, brightness 76, becomes (76, 0, 179) red first.
	uint8_t rgba[] = { 255, 0, 0, 128 };
	ComputeLuminanceRecolor(rgba, 1, kRGBA, table);
	const uint8_t expected[] = { 76, 0, 179, 128 };
	for (int i = 0; i < 4; i++) EXPECT_EQ(rgba[i], expected[i]) << i;
}

TEST(TrueColorFont, AFontKeepsTrueColorOnlyWhenEveryGlyphItHasIsTrueColor)
{
	const GlyphSource none[] = { kGlyphAbsent, kGlyphAbsent };
	const GlyphSource some[] = { kGlyphAbsent, kGlyphTrueColor, kGlyphAbsent };
	const GlyphSource mixed[] = { kGlyphTrueColor, kGlyphPaletted };
	const GlyphSource paletted[] = { kGlyphPaletted };
	EXPECT_FALSE(ComputeFontKeepsTrueColor(nullptr, 0));
	EXPECT_FALSE(ComputeFontKeepsTrueColor(none, 2));
	EXPECT_TRUE(ComputeFontKeepsTrueColor(some, 3));
	EXPECT_FALSE(ComputeFontKeepsTrueColor(mixed, 2));
	EXPECT_FALSE(ComputeFontKeepsTrueColor(paletted, 1));
}
