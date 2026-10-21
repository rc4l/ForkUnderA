// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/levelmesh/computation/shading_compute.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace zx::levelmesh;

namespace {

// [rc4l] The stock settings: GL's default light mode 3, ambient 20, fog mode 1, distfog 70, no level fog.
ShadingEnv Env()
{
	ShadingEnv e = {};
	e.lightMode = 3; e.ambient = 20; e.fogMode = 1; e.distFog = 70;
	return e;
}

ShadingInput Surface(int light, int rel = 0)
{
	ShadingInput in = {};
	in.lightLevel = light; in.relLight = rel; in.lightColor = 0xffffff;
	return in;
}

const float kScale = -1.442692f / 64000.f;

} // namespace

TEST(Shading, LightZeroStaysDarkWhateverTheAmbient)
{
	EXPECT_EQ(ComputeLightLevel(0, 16, false, Env()), 0);
}

TEST(Shading, TheDarkCurveRoundsTiesToEvenAsXsDoes)
{
	// [rc4l] 192 - 10 * 1.95 is exactly 172.5, which xs_CRoundToInt takes to 172.
	EXPECT_EQ(ComputeLightLevel(182, 0, false, Env()), 172);
	ShadingEnv low = Env(); low.ambient = 1;   // [rc4l] so the floor does not hide the curve
	EXPECT_EQ(ComputeLightLevel(100, 0, false, low), 13);
	EXPECT_EQ(ComputeLightLevel(100, 0, false, Env()), 20);   // [rc4l] and with it, 13 is lifted to the ambient 20
	EXPECT_EQ(ComputeLightLevel(200, 0, false, Env()), 200);   // [rc4l] at or above 192 the curve leaves it alone
	EXPECT_EQ(ComputeLightLevel(100, 0, true, Env()), 100);    // [rc4l] and a weapon is never curved
	ShadingEnv flat = Env(); flat.lightMode = 0;
	EXPECT_EQ(ComputeLightLevel(100, 0, false, flat), 100);
}

TEST(Shading, AmbientLiftsDarkLightAndHalvesANegativeRelativeLight)
{
	ShadingEnv e = Env(); e.lightMode = 0; e.ambient = 40;
	EXPECT_EQ(ComputeLightLevel(10, -16, false, e), 32);
	EXPECT_EQ(ComputeLightLevel(10, 16, false, e), 56);
	e.lightMode = 8;   // [rc4l] software lighting clips nothing
	EXPECT_EQ(ComputeLightLevel(10, -16, false, e), 0);
	EXPECT_EQ(ComputeLightLevel(250, 16, false, e), 255);
}

TEST(Shading, LightColorScalesMixesOrPassesThrough)
{
	int r, g, b;
	ComputeLightColor(128, 0xff8040, 0, 3, r, g, b);
	EXPECT_EQ(r, 255 * 128 / 255); EXPECT_EQ(g, 0x80 * 128 / 255); EXPECT_EQ(b, 0x40 * 128 / 255);
	ComputeLightColor(100, 0x204060, 64, 3, r, g, b);
	EXPECT_EQ(r, (100 * 191 + 0x20 * 64) / 255); EXPECT_EQ(g, (100 * 191 + 0x40 * 64) / 255); EXPECT_EQ(b, (100 * 191 + 0x60 * 64) / 255);
	ComputeLightColor(10, 0x123456, 64, 8, r, g, b);
	EXPECT_EQ(r, 0x12); EXPECT_EQ(g, 0x34); EXPECT_EQ(b, 0x56);
}

TEST(Shading, DistFogTablesFollowGLsFormulas)
{
	EXPECT_EQ(ComputeDistFog(70, false, 0), (float)(35 + 70));
	EXPECT_EQ(ComputeDistFog(70, false, 100), (float)(35 + 70 * 64 / 164));
	EXPECT_EQ(ComputeDistFog(70, false, 200), (float)(35 - 35 * 36 / 66));
	EXPECT_EQ(ComputeDistFog(70, false, 240), 0.f);
	EXPECT_EQ(ComputeDistFog(70, true, 100), 6.f + 35 + 70 * 28 / 48);
	EXPECT_EQ(ComputeDistFog(70, true, 150), (216.f - 150) / 88.f * 70 / 10);
	EXPECT_EQ(ComputeDistFog(70, true, 220), 0.f);
}

TEST(Shading, FogDensityFollowsTheLevelsRules)
{
	ShadingEnv e = Env();
	e.lightMode = 4;
	EXPECT_EQ(ComputeFogDensity(100, 0, e), 18.f);   // [rc4l] Legacy's default when the level sets none
	e.fogDensity = 40;
	EXPECT_EQ(ComputeFogDensity(100, 0, e), 40.f);
	e = Env();
	EXPECT_EQ(ComputeFogDensity(300, 0, e), ComputeDistFog(70, true, 255));   // [rc4l] black fog, light clamped into the table
	e.lightMode = 0;
	EXPECT_EQ(ComputeFogDensity(100, 0, e), ComputeDistFog(70, false, 100));
	e.lightMode = 8;
	EXPECT_EQ(ComputeFogDensity(100, 0, e), 0.f);
	e = Env(); e.outsideFogDensity = 50; e.outsideFogColor = 0x00102030;
	EXPECT_EQ(ComputeFogDensity(100, 0xff102030, e), 50.f);   // [rc4l] the fade's alpha does not count
	e.outsideFogColor = 0xff102030;                           // [rc4l] an outside fog marked unset does not apply
	EXPECT_EQ(ComputeFogDensity(100, 0x102030, e), 155.f);
	e.fogDensity = 24;
	EXPECT_EQ(ComputeFogDensity(100, 0x102030, e), 24.f);
	e.fogDensity = 0;
	EXPECT_EQ(ComputeFogDensity(240, 0x102030, e), 30.f);
	EXPECT_EQ(ComputeFogDensity(250, 0x102030, e), 0.f);
}

TEST(Shading, AnOrdinarySurfaceIsLitAndFoggedToBlack)
{
	ShadingInput in = Surface(160, 16);
	in.desaturation = 12;
	const Shading s = ComputeShading(in, Env());
	const int l = ComputeLightLevel(160, 16, false, Env());
	EXPECT_EQ(s.r, l / 255.f); EXPECT_EQ(s.g, l / 255.f); EXPECT_EQ(s.b, l / 255.f);
	EXPECT_EQ(s.desaturation, 12);
	EXPECT_EQ(s.softLight, -1.f);
	EXPECT_EQ(s.fogColor, 0u);
	EXPECT_EQ(s.fogDensity, ComputeDistFog(70, true, 160) * kScale);
	EXPECT_EQ(s.fogMode, 1);
}

TEST(Shading, ColouredFogIsNegativeAndAdditiveFogsToBlack)
{
	ShadingInput in = Surface(128);
	in.fadeColor = 0x40102030;
	Shading s = ComputeShading(in, Env());
	EXPECT_EQ(s.fogColor, 0x102030u);
	EXPECT_EQ(s.fogMode, -1);
	EXPECT_EQ(s.fogDensity, 127.f * kScale);
	in.additive = true;
	s = ComputeShading(in, Env());
	EXPECT_EQ(s.fogColor, 0u);
	EXPECT_EQ(s.fogMode, 1);
}

TEST(Shading, NoFogOrFogModeZeroStoresGLsNegativeZero)
{
	ShadingInput in = Surface(128);
	in.noFog = true;
	Shading s = ComputeShading(in, Env());
	EXPECT_EQ(s.fogMode, 0);
	EXPECT_TRUE(std::signbit(s.fogDensity));
	EXPECT_EQ(s.fogDensity, 0.f);
	ShadingEnv e = Env(); e.fogMode = 0;
	s = ComputeShading(Surface(128), e);
	EXPECT_EQ(s.fogMode, 0);
	EXPECT_EQ(s.fogColor, 0u);
}

TEST(Shading, AFadeTableFogsEverythingGrey)
{
	ShadingEnv e = Env(); e.fadeTable = true;
	ShadingInput in = Surface(200);
	in.noFog = true;
	const Shading s = ComputeShading(in, e);
	EXPECT_EQ(s.fogColor, 0x808080u);
	EXPECT_EQ(s.fogDensity, 70.f * kScale);
	EXPECT_EQ(s.fogMode, -1);
}

TEST(Shading, AFixedColormapIsWhiteUnfoggedAndFullBright)
{
	ShadingEnv e = Env(); e.fixedColormap = true; e.lightMode = 8;
	ShadingInput in = Surface(40);
	in.lightColor = 0x102030; in.desaturation = 9; in.fadeColor = 0x203040;
	const Shading s = ComputeShading(in, e);
	EXPECT_EQ(s.r, 1.f); EXPECT_EQ(s.g, 1.f); EXPECT_EQ(s.b, 1.f);
	EXPECT_EQ(s.desaturation, 0);
	EXPECT_EQ(s.softLight, 1.f);
	EXPECT_EQ(s.fogMode, 0);
}

TEST(Shading, ASkyboxThickensTheFogByHalf)
{
	ShadingEnv e = Env(); e.inSkybox = true; e.fogDensity = 40;
	ShadingInput in = Surface(128);
	in.fadeColor = 0x102030;
	EXPECT_EQ(ComputeShading(in, e).fogDensity, 60.f * kScale);
}

TEST(Shading, SoftwareLightingCarriesTheLevelAndBrightFogRaisesIt)
{
	ShadingEnv e = Env(); e.lightMode = 8;
	ShadingInput in = Surface(100, 20);
	in.fadeColor = 0x102030;
	EXPECT_EQ(ComputeShading(in, e).softLight, 120 / 255.f);
	e.brightFog = true;
	EXPECT_EQ(ComputeShading(in, e).softLight, 1.f);
	in.additive = true;   // [rc4l] black fog is not bright fog
	EXPECT_EQ(ComputeShading(in, e).softLight, 120 / 255.f);
}
