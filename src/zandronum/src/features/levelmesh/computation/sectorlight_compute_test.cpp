// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/levelmesh/computation/sectorlight_compute.h"

using namespace zx::levelmesh;

namespace {

SectorLightEntry Room(int light, unsigned fade = 0)
{
	SectorLightEntry e;
	e.lightLevel = light; e.floorLight = light + 16; e.ceilingLight = light - 16;
	e.lightColor = 0xffffffff; e.fadeColor = fade; e.blendFactor = 3; e.desaturation = 40;
	return e;
}

SectorLightRef Wall(int sideLight, int contrast, bool absolute = false, bool lightFog = false)
{
	SectorLightRef r;
	r.sector = 7; r.kind = kSectorLitWall; r.sideLight = sideLight; r.contrast = contrast;
	r.sideAbsolute = absolute; r.sideLightFog = lightFog;
	return r;
}

}

TEST(ComputeSectorLitInput, AFlatTakesItsPlanesLightAndTheColormap)
{
	SectorLightRef r;
	r.sector = 7;
	const SectorLitInput floor = ComputeSectorLitInput(Room(144), r, false);
	EXPECT_EQ(160, floor.lightLevel);
	EXPECT_EQ(0, floor.relLight);
	EXPECT_EQ(0xffffffffu, floor.lightColor);
	EXPECT_EQ(3, floor.blendFactor);
	EXPECT_EQ(40, floor.desaturation);
	r.kind = kSectorLitCeiling;
	EXPECT_EQ(128, ComputeSectorLitInput(Room(144), r, false).lightLevel);
}

// [rc4l] GLWall::Process over SideLightLevel: the side's light added and clamped, the fake contrast kept as the relative light.
TEST(ComputeSectorLitInput, AWallAddsItsSideLightAndKeepsItsContrast)
{
	SectorLitInput w = ComputeSectorLitInput(Room(144), Wall(16, -16), false);
	EXPECT_EQ(160, w.lightLevel);
	EXPECT_EQ(-16, w.relLight);
	// [rc4l] Clamped either way, and a sector's own light clamped before the side's is added.
	EXPECT_EQ(255, ComputeSectorLitInput(Room(250), Wall(30, 0), false).lightLevel);
	EXPECT_EQ(0, ComputeSectorLitInput(Room(10), Wall(-30, 0), false).lightLevel);
	EXPECT_EQ(240, ComputeSectorLitInput(Room(300), Wall(-15, 0), false).lightLevel);
	// [rc4l] An absolute side light stands alone, still clamped.
	w = ComputeSectorLitInput(Room(144), Wall(200, 8, true), false);
	EXPECT_EQ(200, w.lightLevel);
	EXPECT_EQ(8, w.relLight);
	EXPECT_EQ(255, ComputeSectorLitInput(Room(144), Wall(300, 0, true), false).lightLevel);
}

// [rc4l] SideLightLevel's "no relative lighting in foggy sectors", by a coloured fade or the level's fade table.
TEST(ComputeSectorLitInput, FogDropsTheContrastAndTheSideLightUnlessKeptInFog)
{
	SectorLitInput w = ComputeSectorLitInput(Room(144, 0x00203040), Wall(16, -16), false);
	EXPECT_EQ(144, w.lightLevel);
	EXPECT_EQ(0, w.relLight);
	EXPECT_EQ(160, ComputeSectorLitInput(Room(144, 0x00203040), Wall(16, -16, false, true), false).lightLevel);
	EXPECT_EQ(144, ComputeSectorLitInput(Room(144), Wall(16, -16), true).lightLevel);
	// [rc4l] A fade's alpha alone is no fog.
	EXPECT_EQ(160, ComputeSectorLitInput(Room(144, 0xff000000), Wall(16, -16), false).lightLevel);
}

// [rc4l] GLWall::Process: no contrast at 253 and over, and none that would lift a wall past 256.
TEST(ComputeSectorLitInput, TheRelativeLightIsBoundedAsGLWallBoundsIt)
{
	EXPECT_EQ(0, ComputeSectorLitInput(Room(253), Wall(0, 16), false).relLight);
	SectorLitInput w = ComputeSectorLitInput(Room(240), Wall(15, -16), false);
	EXPECT_EQ(255, w.lightLevel);
	EXPECT_EQ(256 - 255 - 16, w.relLight);
	EXPECT_EQ(16, ComputeSectorLitInput(Room(200), Wall(0, 16), false).relLight);
}

TEST(ComputeSectorLightWords, OneWordAField)
{
	unsigned w[kSectorLightWords];
	ComputeSectorLightWords(Room(144, 0x00102030), w);
	EXPECT_EQ(144u, w[0]);
	EXPECT_EQ(160u, w[1]);
	EXPECT_EQ(128u, w[2]);
	EXPECT_EQ(0xffffffffu, w[3]);
	EXPECT_EQ(0x00102030u, w[4]);
	EXPECT_EQ(3u, w[5]);
	EXPECT_EQ(40u, w[6]);
	EXPECT_EQ(0u, w[7]);
}

// [rc4l] A surface is lit from the table only when its sector gives back every input it was shaded from.
TEST(ComputeSectorLitReproduces, EveryInputMustMatch)
{
	const SectorLitInput in = ComputeSectorLitInput(Room(144), Wall(16, -16), false);
	EXPECT_TRUE(ComputeSectorLitReproduces(in, 160, -16, 0xffffffffu, 0, 3, 40));
	EXPECT_FALSE(ComputeSectorLitReproduces(in, 161, -16, 0xffffffffu, 0, 3, 40));
	EXPECT_FALSE(ComputeSectorLitReproduces(in, 160, -8, 0xffffffffu, 0, 3, 40));
	EXPECT_FALSE(ComputeSectorLitReproduces(in, 160, -16, 0xff8080ffu, 0, 3, 40));
	EXPECT_FALSE(ComputeSectorLitReproduces(in, 160, -16, 0xffffffffu, 0x00101010u, 3, 40));
	EXPECT_FALSE(ComputeSectorLitReproduces(in, 160, -16, 0xffffffffu, 0, 0, 40));
	EXPECT_FALSE(ComputeSectorLitReproduces(in, 160, -16, 0xffffffffu, 0, 3, 0));
}
