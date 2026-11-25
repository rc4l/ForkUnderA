// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <cmath>
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

namespace {

// [Doruk-Aksoy] A 32-wide, 56-tall quad facing a view down +y (perpendicular +x), standing at (100, 0, 200) in scene x, height, map y.
void TicQuad(SpriteCorner c[4])
{
	c[0] = { 84.f, 56.f, 200.f, 0.f, 0.f };
	c[1] = { 116.f, 56.f, 200.f, 1.f, 0.f };
	c[2] = { 84.f, 0.f, 200.f, 0.f, 1.f };
	c[3] = { 116.f, 0.f, 200.f, 1.f, 1.f };
}

SpriteMotion Moving(bool xy) { return SpriteMotion{ 100.f, 200.f, 1.f, 0.f, 8.f, 4.f, -16.f, xy }; }

} // namespace

TEST(SpriteMotionCorners, AtTheTicsEndFromTheTicsViewTheyStandAsStored)
{
	SpriteCorner in[4], out[4];
	TicQuad(in);
	ComputeSpriteMotionCorners(in, Moving(false), 1.f, 1.f, 0.f, 30.f, out);
	for (int k = 0; k < 4; k++)
	{
		EXPECT_FLOAT_EQ(in[k].x, out[k].x);
		EXPECT_FLOAT_EQ(in[k].y, out[k].y);
		EXPECT_FLOAT_EQ(in[k].z, out[k].z);
		EXPECT_EQ(in[k].u, out[k].u);
		EXPECT_EQ(in[k].v, out[k].v);
	}
}

TEST(SpriteMotionCorners, AtTheTicsStartTheWholeMoveIsTakenBack)
{
	SpriteCorner in[4], out[4];
	TicQuad(in);
	ComputeSpriteMotionCorners(in, Moving(false), 0.f, 1.f, 0.f, 0.f, out);
	EXPECT_FLOAT_EQ(76.f, out[0].x);
	EXPECT_FLOAT_EQ(52.f, out[0].y);
	EXPECT_FLOAT_EQ(216.f, out[0].z);
	EXPECT_FLOAT_EQ(108.f, out[3].x);
	EXPECT_FLOAT_EQ(-4.f, out[3].y);
	// [Doruk-Aksoy] Half way, half of it.
	ComputeSpriteMotionCorners(in, Moving(false), 0.5f, 1.f, 0.f, 0.f, out);
	EXPECT_FLOAT_EQ(80.f, out[0].x);
	EXPECT_FLOAT_EQ(208.f, out[0].z);
}

TEST(SpriteMotionCorners, AViewTurnedAQuarterTurnsTheQuadAboutItsCentre)
{
	SpriteCorner in[4], out[4];
	TicQuad(in);
	// [Doruk-Aksoy] The view now looks down -x, so the perpendicular is +y on the map (scene z).
	ComputeSpriteMotionCorners(in, Moving(false), 1.f, 0.f, 1.f, 0.f, out);
	EXPECT_FLOAT_EQ(100.f, out[0].x);
	EXPECT_FLOAT_EQ(184.f, out[0].z);
	EXPECT_FLOAT_EQ(100.f, out[1].x);
	EXPECT_FLOAT_EQ(216.f, out[1].z);
	EXPECT_FLOAT_EQ(56.f, out[1].y);
}

TEST(SpriteMotionCorners, AnXYBillboardLeansBackByThePitchAboutItsMiddle)
{
	SpriteCorner in[4], out[4];
	TicQuad(in);
	// [Doruk-Aksoy] Level, nothing turns.
	ComputeSpriteMotionCorners(in, Moving(true), 1.f, 1.f, 0.f, 0.f, out);
	for (int k = 0; k < 4; k++) { EXPECT_NEAR(in[k].x, out[k].x, 1e-4f); EXPECT_NEAR(in[k].y, out[k].y, 1e-4f); EXPECT_NEAR(in[k].z, out[k].z, 1e-4f); }
	// [Doruk-Aksoy] Pitched 90 degrees the quad lies flat: its top edge swings onto the map about the middle (100, 28, 200), keeping its x.
	ComputeSpriteMotionCorners(in, Moving(true), 1.f, 1.f, 0.f, 90.f, out);
	EXPECT_NEAR(84.f, out[0].x, 1e-4f);
	EXPECT_NEAR(28.f, out[0].y, 1e-4f);
	EXPECT_NEAR(172.f, out[0].z, 1e-4f);
	EXPECT_NEAR(28.f, out[2].y, 1e-4f);
	EXPECT_NEAR(228.f, out[2].z, 1e-4f);
}

TEST(SpriteMotionCorners, TheCosineFormMatchesThePitchForm)
{
	SpriteCorner in[4], a[4], b[4];
	TicQuad(in);
	const float pitch = 23.5f, ang = -pitch * 3.14159265358979323846f / 180.f;
	ComputeSpriteMotionCorners(in, Moving(true), 0.25f, 0.6f, 0.8f, pitch, a);
	ComputeSpriteMotionCornersCS(in, Moving(true), 0.25f, 0.6f, 0.8f, std::cos(ang), std::sin(ang), b);
	for (int k = 0; k < 4; k++) { EXPECT_EQ(a[k].x, b[k].x); EXPECT_EQ(a[k].y, b[k].y); EXPECT_EQ(a[k].z, b[k].z); }
}

// [Doruk-Aksoy] The player's pass draws every run; a camera's pass only the runs behind the tic stream's.
TEST(PassDrawsRun, ACameraPassLeavesOutTheTicStream)
{
	EXPECT_TRUE(ComputePassDrawsRun(0, 3, false));
	EXPECT_TRUE(ComputePassDrawsRun(2, 3, false));
	EXPECT_FALSE(ComputePassDrawsRun(0, 3, true));
	EXPECT_FALSE(ComputePassDrawsRun(2, 3, true));
	EXPECT_TRUE(ComputePassDrawsRun(3, 3, true));
	EXPECT_TRUE(ComputePassDrawsRun(0, 0, true));
}
