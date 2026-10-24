// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-replication/computation/mapstate_compute.h"

#include <gtest/gtest.h>

using namespace zx::netreplication;

TEST(ComputeSectorChanges, NothingChangedSendsNothing)
{
	EXPECT_EQ(0u, ComputeSectorChanges(SectorLook(), SectorLook()));
}

TEST(ComputeSectorChanges, EachFieldNamesItsFamily)
{
	const SectorLook was;
	struct Case { void (*set)(SectorLook &); unsigned family; };
	const Case cases[] = {
		{ [](SectorLook &s) { s.floorPic = 3; }, kSectorFlat },
		{ [](SectorLook &s) { s.ceilingPic = 3; }, kSectorFlat },
		{ [](SectorLook &s) { s.color = 0x40c0ff; }, kSectorColor },
		{ [](SectorLook &s) { s.desaturate = 128; }, kSectorColor },
		{ [](SectorLook &s) { s.fade = 0x101010; }, kSectorFade },
		{ [](SectorLook &s) { s.gravity = 0.5f; }, kSectorGravity },
		{ [](SectorLook &s) { s.friction = 1; }, kSectorFriction },
		{ [](SectorLook &s) { s.moveFactor = 1; }, kSectorFriction },
		{ [](SectorLook &s) { s.reflectFloor = 0.25f; }, kSectorReflection },
		{ [](SectorLook &s) { s.reflectCeiling = 0.25f; }, kSectorReflection },
		{ [](SectorLook &s) { s.xScaleFloor = 2; }, kSectorScale },
		{ [](SectorLook &s) { s.yScaleFloor = 2; }, kSectorScale },
		{ [](SectorLook &s) { s.xScaleCeiling = 2; }, kSectorScale },
		{ [](SectorLook &s) { s.yScaleCeiling = 2; }, kSectorScale },
		{ [](SectorLook &s) { s.angleFloor = 0x40000000; }, kSectorRotation },
		{ [](SectorLook &s) { s.angleCeiling = 0x40000000; }, kSectorRotation },
		{ [](SectorLook &s) { s.special = 1024; }, kSectorSpecial },
	};
	for (const Case &c : cases)
	{
		SectorLook now = was;
		c.set(now);
		EXPECT_EQ(c.family, ComputeSectorChanges(was, now));
	}
}

TEST(ComputeSectorChanges, SeveralFamiliesAtOnce)
{
	SectorLook now;
	now.color = 1;
	now.special = 9;
	now.floorPic = 4;
	EXPECT_EQ(kSectorColor | kSectorSpecial | kSectorFlat, ComputeSectorChanges(SectorLook(), now));
}

TEST(ComputeLineChanges, AlphaFlagsAndEachTexture)
{
	const LineLook was;
	LineLook now = was;
	EXPECT_EQ(0u, ComputeLineChanges(was, now));
	now.alpha = 32768;
	EXPECT_EQ(kLineAlpha, ComputeLineChanges(was, now));
	now = was;
	now.clientFlags = 2;
	EXPECT_EQ(kLineFlags, ComputeLineChanges(was, now));
	for (int i = 0; i < 6; i++)
	{
		now = was;
		now.textures[i] = 7;
		EXPECT_EQ(kLineTexture << i, ComputeLineChanges(was, now));
		EXPECT_EQ(1u << i, ComputeLineTextureParts(ComputeLineChanges(was, now)));
	}
}

TEST(ComputeLineTextureParts, OnlyTheTextureBits)
{
	EXPECT_EQ(0u, ComputeLineTextureParts(kLineAlpha | kLineFlags));
	EXPECT_EQ(0x3Fu, ComputeLineTextureParts(~0u));
}

TEST(ComputeTakeFamilies, OnlyTheNamedFamiliesBecomeSent)
{
	SectorLook sent, now;
	now.color = 0x40c0ff;
	now.floorPic = 5;
	now.special = 3;
	const SectorLook out = ComputeTakeFamilies(sent, now, kSectorColor);
	// [rc4l] A colour sent by tag must not pass for the flat touched in the same tic.
	EXPECT_EQ(kSectorFlat | kSectorSpecial, ComputeSectorChanges(out, now));
	EXPECT_EQ(0u, ComputeSectorChanges(ComputeTakeFamilies(sent, now, ~0u), now));
	SectorLook every;
	every.floorPic = 1; every.ceilingPic = 2; every.color = 3; every.desaturate = 4; every.fade = 5; every.gravity = 6;
	every.friction = 7; every.moveFactor = 8; every.reflectFloor = 9; every.reflectCeiling = 10;
	every.xScaleFloor = 11; every.yScaleFloor = 12; every.xScaleCeiling = 13; every.yScaleCeiling = 14;
	every.angleFloor = 15; every.angleCeiling = 16; every.special = 17;
	for (unsigned family = kSectorFlat; family <= kSectorSpecial; family <<= 1)
		EXPECT_EQ(family, ComputeSectorChanges(SectorLook(), every) & ~ComputeSectorChanges(ComputeTakeFamilies(SectorLook(), every, family), every));
}

TEST(ComputeTakeFamilies, ALinesNamedTexturesAlphaAndFlags)
{
	LineLook sent, now;
	now.alpha = 1;
	now.clientFlags = 2;
	for (int i = 0; i < 6; i++) now.textures[i] = 10 + i;
	EXPECT_EQ(kLineAlpha | kLineFlags | (kLineTexture << 0) | (kLineTexture << 2) | (kLineTexture << 3) | (kLineTexture << 4) | (kLineTexture << 5),
		ComputeLineChanges(ComputeTakeFamilies(sent, now, kLineTexture << 1), now));
	EXPECT_EQ(kLineFlags | (0x3Fu * kLineTexture), ComputeLineChanges(ComputeTakeFamilies(sent, now, kLineAlpha), now));
	EXPECT_EQ(kLineAlpha | (0x3Fu * kLineTexture), ComputeLineChanges(ComputeTakeFamilies(sent, now, kLineFlags), now));
}
