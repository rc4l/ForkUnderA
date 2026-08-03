// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Wire tests for the SetMapSky field layout -- the actual bytes a sky name puts on the stream.
//
// Why this exists alongside features/skywire's unit tests: those pin the 8-character bound as a
// value-level property, which is the thing a refactor is most likely to change by accident. These
// pin the BYTES, because the bound only matters if the encoding around it is what we think it is.
// uzdoom@65e8563cf removed the sky name from FLevelLocals, so the name is now materialised at the
// wire boundary in SERVERCOMMANDS_SetMapSky from FTexture::Name -- and if that ever stops producing
// what these tests assert, clients start decoding a field that no longer matches.
//
// SetMapSky serializes two NUL-terminated strings, sky1 then sky2, via WriteString. That is the
// whole field layout, and it is modelled here directly on BYTESTREAM_s rather than through the
// generated command, because the generated dispatcher resolves live engine state and cannot be
// unit-linked (see the netcode-testing skill).
//
// The honest scope claim: an over-long name would NOT corrupt the packet. WriteString/ReadString
// are self-delimiting and ReadString keeps consuming past its buffer so the stream stays aligned
// to the next field -- which is exactly what the last test here proves, rather than asserting.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s, MAX_NETWORK_STRING

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Writer and reader views over the same bytes, mirroring server-writes / client-reads.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 4096) : buf(n, 0xCC) {   // poison, so "unwritten" is visible
		w.pbStream = buf.data();
		w.pbStreamEnd = buf.data() + buf.size();
	}
	size_t written() const { return static_cast<size_t>(w.pbStream - buf.data()); }
	void openReader() {
		r = BYTESTREAM_s{};
		r.pbStream = buf.data();
		r.pbStreamEnd = buf.data() + written();
	}
};

// The two strings SetMapSky puts on the wire, in order.
void WriteSkyPair(Wire &wire, const char *sky1, const char *sky2) {
	wire.w.WriteString(sky1);
	wire.w.WriteString(sky2);
}

} // namespace

TEST(SkyWireBytes, GoldenTwoNulTerminatedNamesInOrder) {
	// GOLDEN: the exact bytes for a normal pair. sky1 then sky2, each raw ASCII + one NUL.
	Wire wire;
	WriteSkyPair(wire, "SKY1", "SKY2");

	const std::vector<BYTE> expected = {
		'S','K','Y','1', 0x00,
		'S','K','Y','2', 0x00,
	};
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(0, memcmp(wire.buf.data(), expected.data(), expected.size()));
}

TEST(SkyWireBytes, GoldenEightCharacterNameIsNineBytes) {
	// The bound, in bytes: eight characters plus the terminator, and nothing more.
	Wire wire;
	wire.w.WriteString("ABCDEFGH");
	EXPECT_EQ(wire.written(), 9u);
	EXPECT_EQ(wire.buf[8], 0x00);
}

TEST(SkyWireBytes, GoldenEmptySkyIsASingleNul) {
	// An empty sky2 is normal (the engine substitutes sky1). It must cost exactly one byte, or
	// every following field shifts.
	Wire wire;
	WriteSkyPair(wire, "SKY1", "");
	const std::vector<BYTE> expected = { 'S','K','Y','1', 0x00, 0x00 };
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(0, memcmp(wire.buf.data(), expected.data(), expected.size()));
}

TEST(SkyWireBytes, RoundTripsBothNamesInOrder) {
	// ROUND-TRIP: writer and reader must agree on field order and widths.
	Wire wire;
	WriteSkyPair(wire, "RSKY1", "RSKY2");
	wire.openReader();

	// ReadString returns a pointer to a static buffer, so copy before reading the next one.
	const std::string first = wire.r.ReadString();
	const std::string second = wire.r.ReadString();
	EXPECT_EQ(first, "RSKY1");
	EXPECT_EQ(second, "RSKY2");
}

TEST(SkyWireBytes, RoundTripsEmptyNames) {
	Wire wire;
	WriteSkyPair(wire, "", "");
	wire.openReader();
	const std::string first = wire.r.ReadString();
	const std::string second = wire.r.ReadString();
	EXPECT_EQ(first, "");
	EXPECT_EQ(second, "");
}

TEST(SkyWireBytes, AnOverLongNameStaysAlignedToTheNextField) {
	// The claim worth PROVING rather than asserting in a comment: a name longer than the historic
	// eight characters does not desync the packet. The field is self-delimiting, so a following
	// field still decodes correctly -- which is why the 8-char bound is a compatibility matter
	// (clients may not resolve the texture) and not a packet-corruption one.
	Wire wire;
	const char *longName = "skies/nightsky_variant_02";
	WriteSkyPair(wire, longName, "SKY2");
	wire.openReader();

	const std::string first = wire.r.ReadString();
	const std::string second = wire.r.ReadString();
	EXPECT_EQ(first, longName);          // arrives intact, not mangled
	EXPECT_EQ(second, "SKY2");           // and the NEXT field is still aligned
}

TEST(SkyWireBytes, TruncatedPacketDoesNotReadOutOfBounds) {
	// ADVERSARIAL: a packet that ends mid-name. Under ASan this is a hard failure if ReadString
	// walks past the end.
	Wire wire;
	wire.w.WriteString("SKY1");
	wire.openReader();
	wire.r.pbStreamEnd = wire.r.pbStream + 2;   // cut the stream before the terminator

	const std::string got = wire.r.ReadString();
	EXPECT_LE(got.size(), 2u);                  // whatever it salvages, it stays in bounds
}

TEST(SkyWireBytes, ReadingASkyFieldFromAnEmptyStreamIsSafe) {
	// ADVERSARIAL: nothing at all to read.
	Wire wire;
	wire.openReader();                           // zero bytes written
	const std::string got = wire.r.ReadString();
	EXPECT_TRUE(got.empty());
}
