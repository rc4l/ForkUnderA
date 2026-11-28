// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC2_SETSECTORTERRAIN, each plane's own terrain a server script or the map gave a sector (SetSectorTerrain, features/acs-parity).
// [Doruk-Aksoy] Modelled on BYTESTREAM_s in the generator's field order (protocolspec/spec.misc.txt), since the generated dispatcher cannot be unit-linked (netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "wire_nettest.h"     // zx::nettest::Wire
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstddef>
#include <cstring>
#include <string>

using zx::nettest::Wire;

namespace {

// [Doruk-Aksoy] A name as NETWORK_WriteName sends it: a predefined name's index as a short, any other as -1 and its text.
struct SentName { int index; std::string text; };

void WriteName(Wire &wire, const SentName &name) {
	wire.w.WriteShort(name.index);
	if (name.index == -1) wire.w.WriteString(name.text.c_str());
}

void WriteTerrain(Wire &wire, int sector, const SentName &floor, const SentName &ceiling) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_SETSECTORTERRAIN);
	wire.w.WriteShort(sector);
	WriteName(wire, floor);
	WriteName(wire, ceiling);
}

void ReadName(BYTESTREAM_s &r, SentName &name) {
	name.index = static_cast<short>(r.ReadShort());
	name.text = name.index == -1 ? r.ReadString() : "";
}

// [Doruk-Aksoy] What the generated parser reads after the two header bytes; false when it ran past the packet.
bool ReadTerrain(BYTESTREAM_s &r, int &sector, SentName &floor, SentName &ceiling) {
	sector = static_cast<short>(r.ReadShort());
	ReadName(r, floor);
	ReadName(r, ceiling);
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(SectorTerrainWire, IsAppendedAfterTheLastExistingCommand) {
	// [Doruk-Aksoy] The ids shipped before it keep their values, and it fits the SVC2 byte.
	EXPECT_EQ(93, SVC2_SETPLAYERSOUNDCLASS);
	EXPECT_EQ(94, SVC2_SETSECTORTERRAIN);
	EXPECT_EQ(SVC2_SETSECTORTERRAIN + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(SectorTerrainWire, GoldenAFloorTerrainAndTheCeilingsFlat) {
	Wire wire;
	WriteTerrain(wire, 0x0102, { -1, "ZxIce" }, { 0, "" });
	const BYTE expected[] = {
		232, 94,                       // SVC_EXTENDEDCOMMAND, SVC2_SETSECTORTERRAIN
		0x02, 0x01,                    // the sector
		0xFF, 0xFF,                    // the floor's: no predefined name...
		'Z', 'x', 'I', 'c', 'e', 0,    // ...its text
		0x00, 0x00,                    // the ceiling's: None, its flat's own
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SectorTerrainWire, GoldenBothFlatsBackInEightBytes) {
	// [Doruk-Aksoy] NAME_None is predefined name 0, so giving a sector its flats back costs no text.
	Wire wire;
	WriteTerrain(wire, 7, { 0, "" }, { 0, "" });
	const BYTE expected[] = { 232, 94, 7, 0, 0x00, 0x00, 0x00, 0x00 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SectorTerrainWire, RoundTripsEverySectorAndBothNameForms) {
	const SentName names[] = { { -1, "a" }, { -1, std::string(200, 'q') }, { 0, "" }, { 1234, "" } };
	for (int sector : { 0, 1, 255, 256, 32767 })
		for (const SentName &floor : names)
			for (const SentName &ceiling : names) {
				Wire wire;
				WriteTerrain(wire, sector, floor, ceiling);
				wire.openReader();
				ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
				ASSERT_EQ(SVC2_SETSECTORTERRAIN, wire.r.ReadByte());
				int gotSector = -1;
				SentName gotFloor, gotCeiling;
				ASSERT_TRUE(ReadTerrain(wire.r, gotSector, gotFloor, gotCeiling));
				EXPECT_EQ(sector, gotSector);
				EXPECT_EQ(floor.index, gotFloor.index);
				EXPECT_EQ(floor.text, gotFloor.text);
				EXPECT_EQ(ceiling.index, gotCeiling.index);
				EXPECT_EQ(ceiling.text, gotCeiling.text);
			}
}

TEST(SectorTerrainWire, ATruncatedPacketIsRefused) {
	Wire full;
	WriteTerrain(full, 3, { -1, "ZxIce" }, { 0, "" });
	// [Doruk-Aksoy] Cut anywhere before the ceiling's short ends, the reader runs past the packet or reads the ceiling from nothing.
	for (size_t cut = 2; cut < full.written(); cut++) {
		Wire wire;
		WriteTerrain(wire, 3, { -1, "ZxIce" }, { 0, "" });
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		int sector = 0;
		SentName floor, ceiling;
		EXPECT_FALSE(ReadTerrain(wire.r, sector, floor, ceiling)) << cut;
	}
}
