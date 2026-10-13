// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/skyfog_compute.h"

#include <gtest/gtest.h>

using namespace zx::hwrender;

TEST(SkyFog, TheFadeColourAtTheLevelsStrength)
{
	const SkyFog f = ComputeSkyFog(128, false, 0x204060);
	EXPECT_EQ(0x20, f.r);
	EXPECT_EQ(0x40, f.g);
	EXPECT_EQ(0x60, f.b);
	EXPECT_FLOAT_EQ(128 / 255.f, f.a);
	// [rc4l] Clamped to opaque, as GL clamps the alpha it puts on the colour.
	EXPECT_FLOAT_EQ(1.f, ComputeSkyFog(400, false, 0x204060).a);
}

TEST(SkyFog, NoneWithoutSkyfogUnderAFixedColormapOrForBlack)
{
	EXPECT_EQ(0.f, ComputeSkyFog(0, false, 0x204060).a);
	EXPECT_EQ(0.f, ComputeSkyFog(128, true, 0x204060).a);
	EXPECT_EQ(0.f, ComputeSkyFog(128, false, 0xff000000u).a);
}
