// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/draw2d/computation/drawtags_compute.h"

#include <cstdarg>
#include <gtest/gtest.h>

using namespace zx::draw2d;

namespace {

// [Doruk-Aksoy] Leaves its doubles in the caller's outgoing argument slots, as the status bar's right-anchored panel draw left -256.0 there.
double FillSlots(int count, ...)
{
	va_list ap;
	va_start(ap, count);
	double sum = 0;
	for (int i = 0; i < count; i++) sum += va_arg(ap, double);
	va_end(ap);
	return sum;
}

// [Doruk-Aksoy] The alpha tag after `skip` ints, read as ParseDrawTextureTags reads DTA_Alpha.
int64_t ReadAlphaAfter(int skip, ...)
{
	va_list ap;
	va_start(ap, skip);
	for (int i = 0; i < skip; i++) (void)va_arg(ap, int);
	const int64_t alpha = ComputeTagAlpha(va_arg(ap, TagFixed));
	va_end(ap);
	return alpha;
}

} // namespace

TEST(DrawTags, AnIntAlphaPassedOverALeftoverDoubleIsReadWhole)
{
	// [Doruk-Aksoy] DnD's credits: the panel at x -256 left 0xC0700000 in the upper half of the slot its string's int alpha then took.
	volatile double sum = FillSlots(7, -256.0, -256.0, -256.0, -256.0, -256.0, -256.0, -256.0);
	(void)sum;
	EXPECT_EQ(ReadAlphaAfter(6, 1, 2, 3, 4, 5, 6, 0x10000), 0x10000);
	EXPECT_EQ(ReadAlphaAfter(6, 1, 2, 3, 4, 5, 6, 0x8000), 0x8000);
}

TEST(DrawTags, ATagFixedIsTheIntCallersPass)
{
	EXPECT_EQ(sizeof(TagFixed), sizeof(int));
}

TEST(DrawTags, AnAlphaPastOpaqueIsOpaque)
{
	EXPECT_EQ(ComputeTagAlpha(0), 0);
	EXPECT_EQ(ComputeTagAlpha(0x6800), 0x6800);
	EXPECT_EQ(ComputeTagAlpha(0x10000), 0x10000);
	EXPECT_EQ(ComputeTagAlpha(0x30000), 0x10000);
}

TEST(DrawTags, PlainTagValuesRide)
{
	EXPECT_TRUE((ComputeTagArgsAreSafe<>()));
	EXPECT_TRUE((ComputeTagArgsAreSafe<int, unsigned, double, float, bool, char, const char *, void *, va_list *>()));
	EXPECT_TRUE((ComputeTagArgIsSafe<const int &>()));
}

TEST(DrawTags, AFixedOrA64BitIntegerIsRefused)
{
	EXPECT_FALSE((ComputeTagArgIsSafe<zx::Fixed>()));
	EXPECT_FALSE((ComputeTagArgIsSafe<const zx::Fixed &>()));
	EXPECT_FALSE((ComputeTagArgIsSafe<long long>()));
	EXPECT_FALSE((ComputeTagArgIsSafe<unsigned long long>()));
	EXPECT_FALSE((ComputeTagArgsAreSafe<int, double, zx::Fixed>()));
	EXPECT_FALSE((ComputeTagArgsAreSafe<zx::Fixed, int>()));
}
