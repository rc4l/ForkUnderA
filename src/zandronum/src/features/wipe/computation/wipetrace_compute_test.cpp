// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wipe/computation/wipetrace_compute.h"

#include <gtest/gtest.h>

using namespace zx::wipe;

namespace {

// [rc4l] A w by h frame, its left half one colour and its right half another.
std::vector<unsigned char> Halves(int w, int h, unsigned char left, unsigned char right)
{
	std::vector<unsigned char> rgb(w * h * 3);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			for (int c = 0; c < 3; c++) rgb[(y * w + x) * 3 + c] = x < w / 2 ? left : right;
	return rgb;
}

Thumb Flat(unsigned char v) { return Thumb(kThumbW * kThumbH * 3, v); }

} // namespace

TEST(WipeTrace, AFrameShrinksToCellMeans)
{
	const std::vector<unsigned char> rgb = Halves(64, 40, 10, 250);
	Thumb t;
	ComputeThumb(&rgb[0], 64, 40, t);
	ASSERT_EQ(t.size(), (size_t)kThumbW * kThumbH * 3);
	EXPECT_EQ(t[0], 10);                                  // [rc4l] the first cell, wholly in the left half
	EXPECT_EQ(t[(kThumbW - 1) * 3 + 2], 250);             // [rc4l] the last cell of the first row, wholly in the right
}

TEST(WipeTrace, AFrameNarrowerThanTheThumbnailRepeatsItsPixels)
{
	const std::vector<unsigned char> rgb = Halves(4, 2, 0, 200);
	Thumb t;
	ComputeThumb(&rgb[0], 4, 2, t);
	ASSERT_EQ(t.size(), (size_t)kThumbW * kThumbH * 3);
	EXPECT_EQ(t[0], 0);
	EXPECT_EQ(t[((kThumbH - 1) * kThumbW + kThumbW - 1) * 3], 200);
}

TEST(WipeTrace, AnEmptyFrameHasNoThumbnail)
{
	const unsigned char one[3] = { 1, 2, 3 };
	Thumb t = Flat(5);
	ComputeThumb(nullptr, 4, 4, t);
	EXPECT_TRUE(t.empty());
	ComputeThumb(one, 0, 1, t);
	EXPECT_TRUE(t.empty());
	ComputeThumb(one, 1, 0, t);
	EXPECT_TRUE(t.empty());
}

TEST(WipeTrace, TheDistanceIsTheMeanChannelDifferenceOrMinusOne)
{
	EXPECT_DOUBLE_EQ(ComputeThumbDistance(Flat(10), Flat(10)), 0.0);
	EXPECT_DOUBLE_EQ(ComputeThumbDistance(Flat(10), Flat(40)), 30.0);
	EXPECT_DOUBLE_EQ(ComputeThumbDistance(Flat(40), Flat(10)), 30.0);
	EXPECT_DOUBLE_EQ(ComputeThumbDistance(Thumb(), Flat(10)), -1.0);
	EXPECT_DOUBLE_EQ(ComputeThumbDistance(Flat(10), Thumb(3, 10)), -1.0);
}

TEST(WipeTrace, AWipesFramesAreHeldToItsBeforeAndAfter)
{
	WipeRecord w;
	w.before = Flat(0);
	w.after = Flat(100);
	w.frames = { Flat(0), Flat(50), Flat(100) };
	std::vector<double> toBefore, toAfter;
	EXPECT_DOUBLE_EQ(ComputeWipeDistances(w, toBefore, toAfter), 100.0);
	EXPECT_EQ(toBefore, (std::vector<double>{ 0.0, 50.0, 100.0 }));
	EXPECT_EQ(toAfter, (std::vector<double>{ 100.0, 50.0, 0.0 }));
}

TEST(WipeTrace, TheLogKeepsEachWipesFramesWithTheFrameBeforeAndTheFirstAfter)
{
	WipeTraceLog log;
	log.Frame(Flat(1), false);
	log.Frame(Flat(2), false);
	log.NoteStarted();
	log.Frame(Flat(3), true);
	log.Frame(Flat(4), true);
	ASSERT_EQ(log.Wipes().size(), 1u);
	EXPECT_TRUE(log.Wipes()[0].after.empty());   // [rc4l] still running
	log.Frame(Flat(5), false);
	log.Frame(Flat(6), false);
	EXPECT_EQ(log.Started(), 1);
	const WipeRecord &w = log.Wipes()[0];
	EXPECT_EQ(w.before, Flat(2));
	EXPECT_EQ(w.frames, (std::vector<Thumb>{ Flat(3), Flat(4) }));
	EXPECT_EQ(w.after, Flat(5));
}

TEST(WipeTrace, AWipeFromTheFirstFrameHasNoBefore)
{
	WipeTraceLog log;
	log.Frame(Flat(7), true);
	ASSERT_EQ(log.Wipes().size(), 1u);
	EXPECT_TRUE(log.Wipes()[0].before.empty());
	std::vector<double> toBefore, toAfter;
	EXPECT_DOUBLE_EQ(ComputeWipeDistances(log.Wipes()[0], toBefore, toAfter), -1.0);
	EXPECT_EQ(toBefore, (std::vector<double>{ -1.0 }));
}

TEST(WipeTrace, OnlyTheLastWipesAreKept)
{
	WipeTraceLog log;
	for (int i = 0; i < kWipesKept + 2; i++)
	{
		log.Frame(Flat((unsigned char)(i * 10)), true);
		log.Frame(Flat((unsigned char)(i * 10 + 1)), false);
	}
	ASSERT_EQ(log.Wipes().size(), (size_t)kWipesKept);
	EXPECT_EQ(log.Wipes()[0].frames[0], Flat(20));   // [rc4l] the first two dropped
	EXPECT_EQ(log.Wipes().back().after, Flat((unsigned char)((kWipesKept + 1) * 10 + 1)));
}
