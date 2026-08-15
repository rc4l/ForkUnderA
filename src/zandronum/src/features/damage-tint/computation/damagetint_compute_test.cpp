// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/damage-tint/computation/damagetint_compute.h"

using namespace zx::damagetint;

// ---- RampStep --------------------------------------------------------------

TEST(DamageTint, RampRisesAndSaturates)
{
	float v = 0.0f;
	v = RampStep(v, true, 4, 8.0f, 16.0f);
	EXPECT_FLOAT_EQ(v, 0.5f);
	v = RampStep(v, true, 4, 8.0f, 16.0f);
	EXPECT_FLOAT_EQ(v, 1.0f);
	v = RampStep(v, true, 10, 8.0f, 16.0f); // clamped at 1
	EXPECT_FLOAT_EQ(v, 1.0f);
}

TEST(DamageTint, RampFallsAndFloors)
{
	float v = 1.0f;
	v = RampStep(v, false, 8, 8.0f, 16.0f);
	EXPECT_FLOAT_EQ(v, 0.5f);
	v = RampStep(v, false, 8, 8.0f, 16.0f);
	EXPECT_FLOAT_EQ(v, 0.0f);
	v = RampStep(v, false, 8, 8.0f, 16.0f); // clamped at 0
	EXPECT_FLOAT_EQ(v, 0.0f);
}

TEST(DamageTint, RampClampsDtAndHandlesDegenerateRates)
{
	// dt is clamped to 35 tics so a pause never teleports the ramp mid-way in one step...
	float v = RampStep(0.0f, true, 1000, 70.0f, 16.0f);
	EXPECT_FLOAT_EQ(v, 0.5f); // 35/70, not 1000/70
	// negative dt is treated as no time passing
	EXPECT_FLOAT_EQ(RampStep(0.25f, true, -5, 8.0f, 16.0f), 0.25f);
	// degenerate rates snap straight to the target
	EXPECT_FLOAT_EQ(RampStep(0.2f, true, 1, 0.0f, 16.0f), 1.0f);
	EXPECT_FLOAT_EQ(RampStep(0.8f, false, 1, 8.0f, 0.0f), 0.0f);
}

// ---- PulseFactor -----------------------------------------------------------

TEST(DamageTint, PulseSpikesAtCycleStartAndDecays)
{
	EXPECT_FLOAT_EQ(PulseFactor(0, 0.3f, 6), 1.3f);            // damage just landed
	EXPECT_FLOAT_EQ(PulseFactor(3, 0.3f, 6), 1.15f);           // halfway decayed
	EXPECT_FLOAT_EQ(PulseFactor(6, 0.3f, 6), 1.0f);            // at rest
	EXPECT_FLOAT_EQ(PulseFactor(31, 0.3f, 6), 1.0f);           // late in the cycle
}

TEST(DamageTint, PulseDegenerateInputsAreNeutral)
{
	EXPECT_FLOAT_EQ(PulseFactor(0, 0.0f, 6), 1.0f);   // no peak
	EXPECT_FLOAT_EQ(PulseFactor(0, 0.3f, 0), 1.0f);   // no decay window
	EXPECT_FLOAT_EQ(PulseFactor(-1, 0.3f, 6), 1.0f);  // out-of-range tic
}

// ---- EffectivePct ----------------------------------------------------------

TEST(DamageTint, EffectivePctScalesAndClamps)
{
	EXPECT_EQ(EffectivePct(35, 1.0f, 1.0f), 35);
	EXPECT_EQ(EffectivePct(35, 0.5f, 1.0f), 18);   // rounded
	EXPECT_EQ(EffectivePct(35, 1.0f, 1.3f), 46);   // pulsing
	EXPECT_EQ(EffectivePct(100, 1.0f, 1.3f), 100); // clamped high
	EXPECT_EQ(EffectivePct(0, 1.0f, 1.3f), 0);     // disabled
	EXPECT_EQ(EffectivePct(35, 0.0f, 1.3f), 0);    // fully faded out
	EXPECT_EQ(EffectivePct(-5, 1.0f, 1.0f), 0);    // nonsense base
}

// ---- TintChannel -----------------------------------------------------------

TEST(DamageTint, CoverageSpanScalesAndClamps)
{
	EXPECT_FLOAT_EQ(CoverageSpan(50, 56.0f), 28.0f);   // half the marine
	EXPECT_FLOAT_EQ(CoverageSpan(100, 56.0f), 56.0f);  // full body
	EXPECT_FLOAT_EQ(CoverageSpan(0, 56.0f), 0.0f);     // coverage off
	EXPECT_FLOAT_EQ(CoverageSpan(150, 56.0f), 56.0f);  // clamped high
	EXPECT_FLOAT_EQ(CoverageSpan(50, -10.0f), 0.0f);   // nonsense span
	EXPECT_FLOAT_EQ(CoverageSpan(-5, 56.0f), 0.0f);    // nonsense pct
}

TEST(DamageTint, SliceTintFadesLinearlyToCoverageEdge)
{
	EXPECT_EQ(SliceTintPct(40, 0.0f, 0.5f), 40);   // at the feet: full strength
	EXPECT_EQ(SliceTintPct(40, 0.25f, 0.5f), 20);  // halfway to the edge
	EXPECT_EQ(SliceTintPct(40, 0.5f, 0.5f), 0);    // at the coverage edge
	EXPECT_EQ(SliceTintPct(40, 0.8f, 0.5f), 0);    // above it
	EXPECT_EQ(SliceTintPct(40, -0.1f, 0.5f), 40);  // clamped below
	EXPECT_EQ(SliceTintPct(0, 0.0f, 0.5f), 0);     // no strength
	EXPECT_EQ(SliceTintPct(40, 0.2f, 0.0f), 0);    // no coverage
	EXPECT_EQ(SliceTintPct(1000, 0.0f, 1.0f), 100); // clamped to 100
}

TEST(DamageTint, TintChannelBlendsFromWhite)
{
	EXPECT_EQ(TintChannel(0, 0), 255);      // no tint -> neutral white
	EXPECT_EQ(TintChannel(0, 100), 0);      // full tint -> floor color
	EXPECT_EQ(TintChannel(100, 50), 177);   // halfway (255+100)/2 rounded down
	EXPECT_EQ(TintChannel(-20, 100), 0);    // channel clamped low
	EXPECT_EQ(TintChannel(300, 100), 255);  // channel clamped high
	EXPECT_EQ(TintChannel(100, 150), 100);  // pct clamped to 100
}
