// [rc4l] Wire-format regression tests for the sky name in SERVERCOMMANDS_SetMapSky.
//
// The thing under guard is observable protocol behaviour: the sky reaches clients as at most eight
// characters plus a NUL, and has done for as long as the command has existed. uzdoom@65e8563cf
// removed the name from FLevelLocals entirely, so we now derive it at the wire boundary from
// FTexture::Name -- itself char[9], which is what keeps the bound intact for free. Nothing in a
// single-player build exercises this, so it is pinned here.
//
// Scope, stated honestly: this field is written with WriteString and read with ReadString, which
// are NUL-terminated and self-delimiting, and ReadString deliberately keeps consuming an over-long
// string so the packet stays aligned to the next field. A longer name would therefore arrive INTACT
// and parse fine -- it would NOT corrupt the rest of the packet. What these tests protect is
// compatibility: clients receiving a name they may not resolve to a texture.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/skywire/computation/sky_wire_compute.h"

#include <cstring>

using namespace zx;

namespace
{
// [rc4l] Poison the buffer first, so a missing NUL shows up as garbage instead of passing by luck
// on memory that happened to be zero.
struct WireBuf
{
	char b[ZX_SKY_NAME_SIZE];
	WireBuf() { memset(b, 0xAA, sizeof b); }
};
}

TEST(SkyWire, ShortNamesCrossUnchanged)
{
	// The overwhelmingly common case: stock sky names are well under the bound.
	WireBuf w;
	CopySkyNameForWire("SKY1", w.b, sizeof w.b);
	EXPECT_STREQ(w.b, "SKY1");
	EXPECT_TRUE(SkyNameFitsOnWire("SKY1"));
}

TEST(SkyWire, ExactlyEightCharactersIsTheBoundaryAndSurvivesWhole)
{
	// Eight is the largest name that reaches a client intact -- one character more is the cliff.
	WireBuf w;
	CopySkyNameForWire("ABCDEFGH", w.b, sizeof w.b);
	EXPECT_STREQ(w.b, "ABCDEFGH");
	EXPECT_EQ(strlen(w.b), 8u);
	EXPECT_TRUE(SkyNameFitsOnWire("ABCDEFGH"));
}

TEST(SkyWire, NineCharactersIsTruncatedToEight)
{
	// The bound. level_info_t can now hold a long name (that was the point of the FString change);
	// what reaches clients is still bounded by FTexture::Name. If this ever returns nine characters,
	// clients start receiving names they have never had to resolve before -- a compatibility change,
	// and one that should be deliberate rather than a side effect of a refactor.
	WireBuf w;
	CopySkyNameForWire("ABCDEFGHI", w.b, sizeof w.b);
	EXPECT_STREQ(w.b, "ABCDEFGH");
	EXPECT_EQ(strlen(w.b), 8u);
	EXPECT_FALSE(SkyNameFitsOnWire("ABCDEFGHI"));
}

TEST(SkyWire, MuchLongerNamesTruncateToTheSameEight)
{
	// A realistic long texture name from a modern wad, not just a one-over case.
	WireBuf w;
	CopySkyNameForWire("skies/nightsky_variant_02", w.b, sizeof w.b);
	EXPECT_STREQ(w.b, "skies/ni");
	EXPECT_FALSE(SkyNameFitsOnWire("skies/nightsky_variant_02"));
}

TEST(SkyWire, EmptyAndNullAreSentAsEmpty)
{
	// An empty sky2 is normal -- G_GetSecretExitMap-style fallbacks rely on it being empty, and the
	// engine substitutes sky1. It must not become garbage.
	WireBuf w1;
	CopySkyNameForWire("", w1.b, sizeof w1.b);
	EXPECT_STREQ(w1.b, "");
	EXPECT_TRUE(SkyNameFitsOnWire(""));

	WireBuf w2;
	CopySkyNameForWire(nullptr, w2.b, sizeof w2.b);
	EXPECT_STREQ(w2.b, "");
	EXPECT_TRUE(SkyNameFitsOnWire(nullptr));
}

TEST(SkyWire, OutputIsAlwaysTerminatedWhateverTheInput)
{
	// WriteString walks to the NUL; if none exists inside the buffer it runs off the end of the
	// field and corrupts every byte after it in the packet. Note the terminator lands at the END OF
	// THE STRING, not at the last byte -- a short name leaves the tail untouched, exactly as
	// snprintf did -- so the property to assert is "a NUL exists within the buffer", not "the last
	// byte is NUL". (Asserting the latter is what this test did first, and it failed on "A".)
	for (const char *s : { "", "A", "ABCDEFGH", "ABCDEFGHI", "0123456789ABCDEF" })
	{
		WireBuf w;
		CopySkyNameForWire(s, w.b, sizeof w.b);
		const size_t len = strnlen(w.b, sizeof w.b);
		EXPECT_LT(len, sizeof w.b) << "no terminator within the buffer for input: " << s;
		EXPECT_LE(len, 8u) << "input: " << s;
	}
}

TEST(SkyWire, AWiderBufferWouldChangeTheWireAndIsDetectable)
{
	// The refactor that should never land silently: widen the destination and a nine-plus character
	// name suddenly goes out whole. The transport would carry it (ReadString is variable-length),
	// so nothing would break loudly -- clients would simply start seeing names they may not resolve.
	// Asserted here so the boundary is a documented decision rather than an accident of sizeof.
	char wide[32];
	memset(wide, 0xAA, sizeof wide);
	CopySkyNameForWire("ABCDEFGHI", wide, sizeof wide);
	EXPECT_STREQ(wide, "ABCDEFGHI");           // 9 chars -- would be a protocol change
	EXPECT_NE(strlen(wide), 8u);

	// ...whereas at the real wire size the same input is bounded.
	WireBuf w;
	CopySkyNameForWire("ABCDEFGHI", w.b, sizeof w.b);
	EXPECT_EQ(strlen(w.b), 8u);
}

TEST(SkyWire, DegenerateBufferSizesDoNotOverrun)
{
	char one[1] = { (char)0xAA };
	CopySkyNameForWire("SKY1", one, 1);
	EXPECT_EQ(one[0], '\0');

	char guard = (char)0xAA;
	CopySkyNameForWire("SKY1", &guard, 0);     // no room even for the NUL: must write nothing
	EXPECT_EQ(guard, (char)0xAA);

	CopySkyNameForWire("SKY1", nullptr, 9);    // must not crash
}

TEST(SkyWire, TruncationIsIdempotent)
{
	// Round-tripping an already-truncated name through the copy again must not shorten it further;
	// the sky name is re-derived from the loaded texture on savegame load and copied again.
	WireBuf first;
	CopySkyNameForWire("ABCDEFGHI", first.b, sizeof first.b);
	WireBuf second;
	CopySkyNameForWire(first.b, second.b, sizeof second.b);
	EXPECT_STREQ(second.b, first.b);
	EXPECT_STREQ(second.b, "ABCDEFGH");
}
