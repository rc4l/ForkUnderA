// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/draw2d/computation/screenblend_compute.h"

#include <gtest/gtest.h>

using namespace zx::draw2d;

TEST(ScreenBlend, TheFirstBandBelowTheEyeTints)
{
	const BlendBand bands[] = { { 500, 0x80ff0000u, false }, { 100, 0x800000ffu, false }, { -50, 0x8000ff00u, false } };
	EXPECT_EQ(ComputeBandBlend(bands, 3, 200, 0), 0x800000ffu);
	EXPECT_EQ(ComputeBandBlend(bands, 3, 600, 0), 0x80ff0000u);
	EXPECT_EQ(ComputeBandBlend(bands, 3, -100, 0), 0u);   // [rc4l] below every band
}

TEST(ScreenBlend, AFadeWallsBandTheSectorsFadeAndLegacyBlends)
{
	const BlendBand fade[] = { { 100, 0x80ff0000u, true }, { 0, 0x800000ffu, false } };
	EXPECT_EQ(ComputeBandBlend(fade, 2, 200, 0), 0x800000ffu);   // [rc4l] a FADEWALLS floor's band is skipped
	const BlendBand own[] = { { 100, 0xff102030u, false } };
	EXPECT_EQ(ComputeBandBlend(own, 1, 200, 0xff102030u), 0u);   // [rc4l] the sector's own fade is no blend
	const BlendBand legacy[] = { { 100, 0x00102030u, false } };
	EXPECT_EQ(ComputeBandBlend(legacy, 1, 200, 0), 0x80102030u);
	const BlendBand none[] = { { 100, 0u, false } };
	EXPECT_EQ(ComputeBandBlend(none, 1, 200, 5), 0u);
}

TEST(ScreenBlend, AColormapsAverageIsBrightenedToThePalette)
{
	EXPECT_EQ(ComputeBrightenedBlend(0xff402010u, 128), 0xff7f3f1fu);   // [rc4l] scaled by 255/128
	EXPECT_EQ(ComputeBrightenedBlend(0xffc08040u, 128), 0xffffaa55u);   // [rc4l] its own red is the brightest
	EXPECT_EQ(ComputeBrightenedBlend(0x80402010u, 128), 0x80402010u);   // [rc4l] not a colormap's
	EXPECT_EQ(ComputeBrightenedBlend(0xff000000u, 0), 0xff000000u);
}

TEST(ComputeApplyScreenFill, AFillMultipliesOrBlendsOverEachPixel)
{
	unsigned char rgb[6] = { 200, 100, 50, 0, 255, 255 };
	zx::draw2d::ComputeApplyScreenFill(0.5f, 1.f, 0.f, 1.f, true, rgb, 2);
	EXPECT_EQ(100, rgb[0]);
	EXPECT_EQ(100, rgb[1]);
	EXPECT_EQ(0, rgb[2]);
	EXPECT_EQ(255, rgb[4]);
	unsigned char over[3] = { 0, 200, 100 };
	zx::draw2d::ComputeApplyScreenFill(1.f, 0.f, 2.f, 0.5f, false, over, 1);
	EXPECT_EQ(128, over[0]);
	EXPECT_EQ(100, over[1]);
	EXPECT_EQ(255, over[2]);
	unsigned char under[3] = { 10, 10, 10 };
	zx::draw2d::ComputeApplyScreenFill(-1.f, 0.f, 0.f, 1.f, false, under, 1);
	EXPECT_EQ(0, under[0]);
}
