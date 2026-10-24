// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <cstring>

#include "features/hwrender/computation/sectorlitrecord_compute.h"
#include "features/hwrender/computation/desaturate_compute.h"
#include "features/hwrender/computation/texflags_compute.h"
#include "features/levelmesh/computation/packedcolor_compute.h"

using namespace zx::hwrender;
using namespace zx::levelmesh;

namespace {

ShadingEnv Env(int lightMode, int fogMode)
{
	ShadingEnv e;
	e.lightMode = lightMode; e.ambient = 0; e.fogMode = fogMode; e.distFog = 70;
	e.fixedColormap = false; e.fadeTable = false; e.inSkybox = false; e.brightFog = true;
	e.fogDensity = 0; e.outsideFogDensity = 0; e.outsideFogColor = 0xff000000;
	return e;
}

SectorLightEntry Room(unsigned fade)
{
	SectorLightEntry e;
	e.lightLevel = 144; e.floorLight = 160; e.ceilingLight = 128;
	e.lightColor = 0xffffe0c0; e.fadeColor = fade; e.blendFactor = 0; e.desaturation = 64;
	return e;
}

// [rc4l] A record as FillPieceDataWith writes it for a surface ShadePiece baked from in under env.
ScenePieceData Baked(const ShadingInput &in, const ShadingEnv &env)
{
	ShadingEnv level = env;
	level.fixedColormap = false;
	const Shading s = ComputeShading(in, level);
	ScenePieceData pd = ScenePieceData();
	pd.r = s.r; pd.g = s.g; pd.b = s.b; pd.softLight = s.softLight;
	pd.fogDensity = s.fogDensity; pd.fogMode = (float)s.fogMode;
	ComputeUnpackRGB(s.fogColor, pd.fogR, pd.fogG, pd.fogB);
	pd.desaturation = ComputeDesaturationFactor(s.desaturation);
	pd.alpha = 1.f; pd.matSlot = 12.f; pd.warpStyle = 1.f; pd.texFlags = (float)kTexFlagLevel;
	return pd;
}

// [rc4l] Marks a copy of baked from r, then shades it back from e, expecting exactly what was baked.
void ExpectShadedAsBaked(const ScenePieceData &baked, const SectorLightRef &r, bool noFog, const SectorLightEntry &e, const ShadingEnv &env, int extra)
{
	ScenePieceData pd = baked;
	ComputeSectorLitRecord(pd, r, noFog);
	EXPECT_EQ(kTexFlagLevel | kTexFlagSectorLit, (unsigned)pd.texFlags);
	EXPECT_EQ((float)r.sector, pd.r);
	ComputeSectorLitShade(pd, e, env, extra);
	pd.texFlags = baked.texFlags;
	EXPECT_EQ(0, std::memcmp(&pd, &baked, sizeof(pd)));
}

ShadingInput InputFor(const SectorLitInput &lit, int extra, bool noFog)
{
	ShadingInput in;
	in.lightLevel = lit.lightLevel; in.relLight = lit.relLight + extra;
	in.lightColor = lit.lightColor; in.fadeColor = lit.fadeColor;
	in.blendFactor = lit.blendFactor; in.desaturation = lit.desaturation;
	in.noFog = noFog; in.additive = false;
	return in;
}

}

// [rc4l] A wall, a floor and a ceiling come back from the table as baked, in each light mode, black-fogged and in a coloured fade, with the gun's light and without.
TEST(ComputeSectorLitShade, EverySurfaceComesBackAsBaked)
{
	SectorLightRef wall;
	wall.sector = 41; wall.kind = kSectorLitWall; wall.sideLight = 16; wall.contrast = -16;
	SectorLightRef floor;
	floor.sector = 41; floor.kind = kSectorLitFloor;
	SectorLightRef ceiling = floor;
	ceiling.kind = kSectorLitCeiling;
	const SectorLightRef refs[3] = { wall, floor, ceiling };
	const int modes[5] = { 0, 1, 2, 4, 8 };
	const unsigned fades[2] = { 0, 0x00203040 };
	for (const SectorLightRef &r : refs)
		for (int mode : modes)
			for (unsigned fade : fades)
				for (int extra : { 0, 16 })
					for (bool noFog : { false, true })
					{
						const ShadingEnv env = Env(mode, 1);
						const SectorLightEntry e = Room(fade);
						const ShadingInput in = InputFor(ComputeSectorLitInput(e, r, env.fadeTable), extra, noFog);
						SCOPED_TRACE(testing::Message() << "kind " << r.kind << " mode " << mode << " fade " << fade << " extra " << extra << " noFog " << noFog);
						ExpectShadedAsBaked(Baked(in, env), r, noFog, e, env, extra);
					}
}

TEST(ComputeSectorLitShade, AnAbsoluteSideAndOneKeptInFogComeBackAsBaked)
{
	SectorLightRef r;
	r.sector = 3; r.kind = kSectorLitWall; r.sideLight = 200; r.sideAbsolute = true; r.contrast = 8;
	const ShadingEnv env = Env(2, 2);
	SectorLightEntry e = Room(0);
	ExpectShadedAsBaked(Baked(InputFor(ComputeSectorLitInput(e, r, false), 0, false), env), r, false, e, env, 0);
	r.sideAbsolute = false; r.sideLightFog = true; r.sideLight = 24;
	e = Room(0x00102030);
	ExpectShadedAsBaked(Baked(InputFor(ComputeSectorLitInput(e, r, false), 0, false), env), r, false, e, env, 0);
}

TEST(ComputeSectorLightCapacity, AQuarterSpareAndAFloor)
{
	EXPECT_EQ(1024u, ComputeSectorLightCapacity(0));
	EXPECT_EQ(1024u, ComputeSectorLightCapacity(-5));
	EXPECT_EQ(1024u, ComputeSectorLightCapacity(800));
	EXPECT_EQ(4430u, ComputeSectorLightCapacity(3544));
}
