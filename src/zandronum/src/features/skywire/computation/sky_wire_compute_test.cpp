// [rc4l] Wire-format regression tests for the sky name in SERVERCOMMANDS_SetMapSky.
//
// These tests used to assert the opposite of what they assert now, and that is the point worth
// recording. They pinned an 8-character truncation, with one test named
// "AWiderBufferWouldChangeTheWireAndIsDetectable" whose comment said widening "should be a
// deliberate decision rather than an accident of sizeof". uzdoom@59885b856 turned FTexture::Name
// into an FString, the static_assert in sv_commands.cpp broke the build, and this is that
// deliberate decision being made: the sky name now crosses the wire WHOLE.
//
// Why widening is safe rather than merely convenient -- spec.map.txt declares sky1/sky2 as String,
// so the field is variable-length, NUL-terminated and self-delimiting; ReadString hands it straight
// to TexMan.GetTexture with no fixed buffer on the client. Length never affected packet alignment.
// And truncation was not a harmless fallback: "skies/night_a" cut to "skies/ni" can resolve to a
// DIFFERENT texture, so the client renders the wrong sky silently. Nothing regresses either, since
// before 59885b856 no texture could hold a name longer than eight characters at all.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"
#include "features/skywire/computation/sky_wire_compute.h"

#include <cstring>

using namespace zx;

TEST(SkyWire, ShortNamesCrossUnchanged)
{
	// The overwhelmingly common case: stock sky names, well under the old bound.
	EXPECT_STREQ(SkyNameForWire("SKY1"), "SKY1");
}

TEST(SkyWire, EightCharactersIsNoLongerSpecial)
{
	// Eight used to be the cliff. It is now an unremarkable length like any other.
	EXPECT_STREQ(SkyNameForWire("ABCDEFGH"), "ABCDEFGH");
}

TEST(SkyWire, NineCharactersSurvivesWhole)
{
	// The exact case the old tests asserted would be cut to eight. This assertion flipping is the
	// behaviour change; if it ever flips back, truncation has been reintroduced.
	EXPECT_STREQ(SkyNameForWire("ABCDEFGHI"), "ABCDEFGHI");
	EXPECT_EQ(strlen(SkyNameForWire("ABCDEFGHI")), 9u);
}

TEST(SkyWire, LongPathStyleNamesSurviveWhole)
{
	// A realistic name from a modern pk3 -- the reason the limit was lifted upstream. Truncating
	// this to "skies/ni" is what could silently select an unrelated texture on the client.
	const char *n = "skies/nightsky_variant_02";
	EXPECT_STREQ(SkyNameForWire(n), n);
	EXPECT_EQ(strlen(SkyNameForWire(n)), strlen(n));
}

TEST(SkyWire, EmptyAndNullBothBecomeEmpty)
{
	// An empty sky2 is normal -- the engine substitutes sky1. Null happens when the sky did not
	// resolve to a texture at all. Neither may reach WriteString as a null pointer, which would
	// walk off into unrelated memory looking for a NUL.
	EXPECT_STREQ(SkyNameForWire(""), "");
	EXPECT_STREQ(SkyNameForWire(nullptr), "");
	EXPECT_NE(SkyNameForWire(nullptr), nullptr);
}

TEST(SkyWire, ResultIsAlwaysReadableAsAString)
{
	// Whatever goes in, what comes out is a NUL-terminated string safe to hand to WriteString.
	for (const char *s : { "", "A", "ABCDEFGH", "ABCDEFGHI", "0123456789ABCDEF", "skies/a/b/c" })
	{
		const char *out = SkyNameForWire(s);
		ASSERT_NE(out, nullptr) << "input: " << s;
		EXPECT_EQ(strlen(out), strlen(s)) << "input: " << s;
	}
}

TEST(SkyWire, PassThroughIsIdempotent)
{
	// The name is re-derived from the loaded texture on savegame load and sent again; a second trip
	// through the boundary must not change it.
	const char *once = SkyNameForWire("skies/nightsky_variant_02");
	EXPECT_STREQ(SkyNameForWire(once), once);
}
