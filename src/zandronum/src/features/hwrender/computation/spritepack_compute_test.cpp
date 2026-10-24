// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <cstring>
#include <functional>

#include "features/hwrender/computation/spritepack_compute.h"

using namespace zx::hwrender;

namespace {

// [rc4l] A monster as the Complex Doom fight's frame held one: lit from bytes, black-fogged, CPU-lit and half see-through.
ScenePieceData Monster()
{
	ScenePieceData pd = ScenePieceData();
	pd.r = 160 / 255.f; pd.g = 144 / 255.f; pd.b = 128 / 255.f; pd.softLight = -1.f;
	pd.fogDensity = -0.000225f; pd.alpha = 0.5f; pd.fogMode = 1.f; pd.matSlot = 2260.f;
	pd.warpSpeed = 1.f;
	pd.actorR = 0.1234567f; pd.actorG = 0.25f; pd.actorB = 0.f; pd.cpuLight = 1.f;
	pd.brightSlot = 2122.f;
	pd.texFlags = 2.f;
	return pd;
}

// [rc4l] Its billboard, corners in kQuadOrder's order: top two, then bottom two.
void Quad(SpriteCorner c[4])
{
	c[0] = { 2231.761f, -4075.f, 2423.833f, 1.f, 0.f };
	c[1] = { 2228.421f, -4075.f, 2392.159f, 0.f, 0.f };
	c[2] = { 2231.761f, -4096.75f, 2423.833f, 1.f, 1.f };
	c[3] = { 2228.421f, -4096.75f, 2392.159f, 0.f, 1.f };
}

// [rc4l] Packs and unpacks every corner, expecting the record and corners back bit for bit.
void ExpectRoundTrip(const ScenePieceData &pd, const SpriteCorner c[4])
{
	unsigned words[kSpriteWords];
	ASSERT_TRUE(ComputeSpritePack(pd, c, words));
	for (int k = 0; k < 4; k++)
	{
		ScenePieceData back;
		SpriteCorner corner;
		ComputeSpriteUnpack(words, k, back, corner);
		EXPECT_EQ(0, std::memcmp(&back, &pd, sizeof(pd))) << "corner " << k;
		EXPECT_EQ(0, std::memcmp(&corner, &c[k], sizeof(corner))) << "corner " << k;
	}
}

// [rc4l] Whether the monster still packs once edit has changed it.
bool PacksAfter(const std::function<void(ScenePieceData &)> &edit)
{
	ScenePieceData pd = Monster();
	edit(pd);
	SpriteCorner c[4];
	Quad(c);
	unsigned words[kSpriteWords];
	return ComputeSpritePack(pd, c, words);
}

}

TEST(ComputeSpritePack, AMonsterComesBackBitForBit)
{
	SpriteCorner c[4];
	Quad(c);
	ExpectRoundTrip(Monster(), c);
}

TEST(ComputeSpritePack, MirroredAndCollapsedTextureCoordinatesComeBack)
{
	SpriteCorner c[4];
	Quad(c);
	// [rc4l] A rotation drawn mirrored swaps u across the quad.
	for (SpriteCorner &k : c) k.u = 1.f - k.u;
	ExpectRoundTrip(Monster(), c);
	// [rc4l] A sliver whose two edges share one u.
	for (SpriteCorner &k : c) k.u = 0.5f;
	ExpectRoundTrip(Monster(), c);
	// [rc4l] Coloured fog's negative fog mode, a coloured fade and no CPU light.
	ScenePieceData pd = Monster();
	pd.fogMode = -2.f; pd.fogR = 1.f; pd.fogG = 32 / 255.f; pd.fogB = 0.f; pd.cpuLight = 0.f; pd.desaturation = 0.75f;
	ExpectRoundTrip(pd, c);
}

TEST(ComputeSpritePack, AnythingThePackedFormCannotCarryStaysWhole)
{
	EXPECT_TRUE(PacksAfter([](ScenePieceData &) {}));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.warpStyle = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.warpSpeed = 2.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.nz = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.flood = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.stackId = 3.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.animates = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.plain = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.scrollV = 0.5f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.glowTop[3] = 64.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.glowBottom[0] = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.glowTopPlane[1] = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.glowBottomPlane[2] = 1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.tint[3] = 0.5f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.tintRange[0] = 1.f; }));
	// [rc4l] A colour a thing's tint multiplied off the 8-bit steps, or out of range.
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.r = (160 / 255.f) * (200 / 255.f); }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.g = 1.5f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.b = -0.25f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.fogG = 0.3f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.fogMode = 0.5f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.fogMode = 200.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.matSlot = 70000.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.brightSlot = -1.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.texFlags = 65536.f; }));
	EXPECT_FALSE(PacksAfter([](ScenePieceData &p) { p.cpuLight = 0.5f; }));
}

TEST(ComputeSpritePack, TextureCoordinatesThatAreNoRectangleStayWhole)
{
	ScenePieceData pd = Monster();
	SpriteCorner c[4];
	unsigned words[kSpriteWords];
	Quad(c);
	c[3].u = 0.5f;
	EXPECT_FALSE(ComputeSpritePack(pd, c, words));
	Quad(c);
	c[2].v = 0.25f;
	EXPECT_FALSE(ComputeSpritePack(pd, c, words));
}

TEST(ComputeSpriteRef, TheTopBitTellsACornerFromAVertex)
{
	EXPECT_EQ(0x8000001fu, ComputeSpriteRef(28, 3));
	EXPECT_EQ(0x80000000u, ComputeSpriteRef(0, 0));
	EXPECT_EQ(5u, ComputeGeneralRef(5));
	EXPECT_EQ(0x7fffffffu, ComputeGeneralRef(0xffffffffu));
}
