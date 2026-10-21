// [rc4l] Tests for the prompt-panel geometry/gradient helper. Exercises every line (the coverage gate
// enforces 100% on *_compute.cpp). Recovered with the helper from the old crash-consent panel.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/updater/computation/promptpanel_compute.h"

#include <gtest/gtest.h>

using namespace zx;

TEST(PanelRect, CentresAndWrapsContent)
{
	// 640x480 screen, 300-wide panel, content rows [100,300], padY 12.
	PanelRect r = ComputePanelRect(640, 480, 300, 100, 300, 12, 10);
	EXPECT_EQ(r.w, 300);
	EXPECT_EQ(r.x, (640 - 300) / 2);   // centred
	EXPECT_EQ(r.y, 100 - 12);          // contentTop - padY
	EXPECT_EQ(r.h, (300 + 12) - (100 - 12));
	EXPECT_EQ(r.radius, 10);           // small radius, unclamped
}

TEST(PanelRect, WidthClampedToScreen)
{
	// panelW wider than the screen -> ClampInt high branch.
	PanelRect r = ComputePanelRect(400, 400, 999, 50, 150, 8, 6);
	EXPECT_EQ(r.w, 400);
	EXPECT_EQ(r.x, 0);
}

TEST(PanelRect, NegativeWidthClampedToZero)
{
	// panelW negative -> ClampInt low branch.
	PanelRect r = ComputePanelRect(400, 400, -50, 50, 150, 8, 6);
	EXPECT_EQ(r.w, 0);
}

TEST(PanelRect, ClampsTopBottomAndRadius)
{
	// contentTop-padY < 0 -> top=0 ; contentBottom+padY > screenH -> bottom=screenH.
	PanelRect r = ComputePanelRect(200, 120, 180, 5, 200, 10, 999);
	EXPECT_EQ(r.y, 0);                 // top clamped to 0
	EXPECT_EQ(r.h, 120);               // bottom clamped to screenH (200+10 -> 120), top 0
	// radius clamped to half the smaller side (min(w=180,h=120)/2 = 60).
	EXPECT_EQ(r.radius, 60);
}

TEST(PanelRect, DegenerateHeightAndScreen)
{
	// contentBottom+padY < contentTop-padY -> h < 0 -> h=0 ; screenW <= 0 -> maxW=0 -> w=0.
	PanelRect r = ComputePanelRect(0, 100, 50, 90, 10, 0, 4);
	EXPECT_EQ(r.w, 0);
	EXPECT_EQ(r.h, 0);
	EXPECT_EQ(r.radius, 0);            // half of min(0,0)=0
}

TEST(CenteredTop, CentresAndClamps)
{
	EXPECT_EQ(ComputeCenteredTop(480, 200), 140); // (480-200)/2
	EXPECT_EQ(ComputeCenteredTop(100, 200), 0);   // block taller than screen -> clamped to 0
}

TEST(RoundedInset, ZeroRadiusOrOutOfRange)
{
	EXPECT_EQ(ComputeRoundedInset(5, 100, 0), 0);   // radius <= 0
	EXPECT_EQ(ComputeRoundedInset(-1, 100, 10), 0); // row < 0
	EXPECT_EQ(ComputeRoundedInset(100, 100, 10), 0); // row >= height
}

TEST(RoundedInset, MiddleBandNoInset)
{
	// A row far from both edges (v >= radius) has no inset.
	EXPECT_EQ(ComputeRoundedInset(50, 100, 10), 0);
}

TEST(RoundedInset, CornerFollowsCircle)
{
	// Top edge (row 0): full inset == radius.
	EXPECT_EQ(ComputeRoundedInset(0, 100, 10), 10);
	// Bottom edge (row height-1) is symmetric.
	EXPECT_EQ(ComputeRoundedInset(99, 100, 10), 10);
	// Interior corner row: inset = radius - floor(sqrt(radius^2 - (radius-v)^2)).
	// radius=10, row=2 -> v=2, d=8, sqrt(100-64)=6 -> inset=4.
	EXPECT_EQ(ComputeRoundedInset(2, 100, 10), 4);
	// Inset shrinks monotonically toward the straight part.
	EXPECT_GT(ComputeRoundedInset(0, 100, 10), ComputeRoundedInset(3, 100, 10));
}

TEST(Gradient, EndpointsAndMidpoint)
{
	PanelColor top = { 100, 60, 20, 240 };
	PanelColor bot = { 0, 0, 0, 200 };

	PanelColor a = ComputePanelGradient(0, 101, top, bot);
	EXPECT_EQ(a.r, 100);
	EXPECT_EQ(a.a, 240);

	PanelColor z = ComputePanelGradient(100, 101, top, bot);
	EXPECT_EQ(z.r, 0);
	EXPECT_EQ(z.a, 200);

	PanelColor m = ComputePanelGradient(50, 101, top, bot);
	EXPECT_EQ(m.r, 50);   // halfway between 100 and 0
	EXPECT_EQ(m.a, 220);  // halfway between 240 and 200
}

TEST(Gradient, SingleRowAndClamp)
{
	PanelColor top = { 30, 30, 30, 255 };
	PanelColor bot = { 10, 10, 10, 100 };

	// height <= 1 -> den forced to 1; row 0 yields the top colour.
	PanelColor one = ComputePanelGradient(0, 1, top, bot);
	EXPECT_EQ(one.r, 30);
	EXPECT_EQ(one.a, 255);

	// row beyond range is clamped to the last row (bottom colour).
	PanelColor over = ComputePanelGradient(999, 5, top, bot);
	EXPECT_EQ(over.r, 10);
	EXPECT_EQ(over.a, 100);

	// negative row clamps to the top colour.
	PanelColor under = ComputePanelGradient(-4, 5, top, bot);
	EXPECT_EQ(under.r, 30);
}

//=============================================================================
// ComputeSeparatorAlpha -- the fading horizontal rule inside a panel
//=============================================================================

TEST(SeparatorAlpha, PeaksInTheMiddle)
{
	EXPECT_EQ(200, zx::ComputeSeparatorAlpha(50, 101, 200));
}

TEST(SeparatorAlpha, FadesToNothingAtBothEnds)
{
	// The point of the fade: a rule inside a panel with no border of its own must not end in a hard
	// edge, or it reads as the panel being cut in two.
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(0, 101, 200));
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(100, 101, 200));
}

TEST(SeparatorAlpha, RisesMonotonicallyTowardsTheMiddle)
{
	int prev = -1;
	for (int x = 0; x <= 50; ++x)
	{
		const int a = zx::ComputeSeparatorAlpha(x, 101, 200);
		EXPECT_GE(a, prev) << "dipped at x=" << x;
		prev = a;
	}
	EXPECT_EQ(200, prev);
}

TEST(SeparatorAlpha, IsSymmetric)
{
	for (int x = 0; x < 50; ++x)
		EXPECT_EQ(zx::ComputeSeparatorAlpha(x, 101, 200),
			zx::ComputeSeparatorAlpha(100 - x, 101, 200)) << "asymmetric at x=" << x;
}

TEST(SeparatorAlpha, NeverExceedsThePeak)
{
	for (int x = 0; x < 64; ++x)
		EXPECT_LE(zx::ComputeSeparatorAlpha(x, 64, 255), 255);
}

TEST(SeparatorAlpha, RefusesColumnsOutsideTheRule)
{
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(-1, 101, 200));
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(101, 101, 200));
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(999, 101, 200));
}

TEST(SeparatorAlpha, DegenerateWidthsAndPeaksDrawNothing)
{
	// A panel narrow enough to have no room for a rule asks for one anyway on some resolutions; it
	// must come back with nothing to draw rather than dividing by a zero half-width.
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(0, 0, 200));
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(0, -5, 200));
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(0, 101, 0));
	EXPECT_EQ(0, zx::ComputeSeparatorAlpha(0, 101, -1));
}

TEST(SeparatorAlpha, AOneColumnRuleIsJustThePeak)
{
	EXPECT_EQ(200, zx::ComputeSeparatorAlpha(0, 1, 200));
}
