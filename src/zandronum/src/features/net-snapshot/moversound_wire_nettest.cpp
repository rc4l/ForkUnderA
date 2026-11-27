// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: moversound_wire_nettest.cpp
//
// Description: The bytes of the three sector-keyed mover sounds a gated client
// needs -- SVC2_DOORSOUND, SVC2_MOVERSOUND and SVC2_FLOORSOUND -- none of which
// had its payload pinned: all three are written by hand in sv_commands.cpp, so the
// protocol golden sees only their ids. Modelled on BYTESTREAM_s rather than the generated command, which
// resolves live engine state and cannot be unit-linked (the netcode-testing skill);
// faithful because NetCommand::addInteger and BYTESTREAM_s::WriteShort write the
// same little-endian bytes. A sector index of 32,768 or more is out of scope here:
// ReadShort casts to (short), so it arrives negative and every one of the three
// readers drops it on `sectorID < 0`, which is engine-wide and widens on its own.
//
//-----------------------------------------------------------------------------

#include "doomtype.h"						// BYTE and friends, before networkshared.h
#include "networkshared.h"					// BYTESTREAM_s
#include "features/net-snapshot/format.h"	// SNAPSND_PACK and the kinds it packs
#include "wire_nettest.h"					// zx::nettest::Wire

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using zx::nettest::Wire;

namespace
{

// SERVERCOMMANDS_DoorSound: the sector, then the choice with the check-sequence flag above it.
void WriteDoorSound(Wire &wire, int sector, int choice, bool checkSequence)
{
	wire.w.WriteShort(sector);
	wire.w.WriteByte((choice & 3) | (checkSequence ? 4 : 0));
}

// SERVERCOMMANDS_MoverSound: the sector, then SNAPSND_PACK's kind above the class's own choice.
void WriteMoverSound(Wire &wire, int sector, int kind, int choice)
{
	wire.w.WriteShort(sector);
	wire.w.WriteByte(SNAPSND_PACK(kind, choice));
}

// SERVERCOMMANDS_FloorSound: the sector, and nothing after it.
void WriteFloorSound(Wire &wire, int sector)
{
	wire.w.WriteShort(sector);
}

} // namespace

TEST(MoverSoundWireBytes, GoldenDoorSoundIsTheSectorThenThePackedChoice)
{
	// GOLDEN: sector 291 little-endian, then choice 2 with the check-sequence flag set.
	Wire wire;
	WriteDoorSound(wire, 291, 2, true);

	const std::vector<BYTE> expected = { 0x23, 0x01, 0x06 };
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(0, memcmp(wire.buf.data(), expected.data(), expected.size()));
}

TEST(MoverSoundWireBytes, GoldenDoorSoundCarriesCheckSequenceInBitTwo)
{
	// GOLDEN, every combination the writer can produce: the choice in the low two bits and the
	// flag at 4, which is what OnDoorSound reads back as `choice & 3` and `choice & 4`.
	for (int choice = 0; choice <= 3; ++choice)
	{
		for (int flag = 0; flag <= 1; ++flag)
		{
			Wire wire;
			WriteDoorSound(wire, 7, choice, flag != 0);
			ASSERT_EQ(wire.written(), 3u) << "choice " << choice << " flag " << flag;
			EXPECT_EQ(wire.buf[0], 0x07);
			EXPECT_EQ(wire.buf[1], 0x00);
			EXPECT_EQ(wire.buf[2], (BYTE)(choice | (flag ? 4 : 0)))
				<< "choice " << choice << " flag " << flag;
		}
	}
}

TEST(MoverSoundWireBytes, DoorSoundRoundTripsItsSectorChoiceAndFlag)
{
	// ROUND-TRIP: through the reader's own widths and order, decoded as OnDoorSound decodes it.
	for (int choice = 0; choice <= 3; ++choice)
	{
		for (int flag = 0; flag <= 1; ++flag)
		{
			Wire wire;
			WriteDoorSound(wire, 1234, choice, flag != 0);
			wire.openReader();

			const int sectorID = wire.r.ReadShort();
			const int packed = wire.r.ReadByte();
			EXPECT_EQ(sectorID, 1234);
			EXPECT_EQ(packed & 3, choice);
			EXPECT_EQ((packed & 4) != 0, flag != 0);
		}
	}
}

TEST(MoverSoundWireBytes, GoldenMoverSoundIsTheSectorThenKindAboveChoice)
{
	// GOLDEN: sector 5, a plat's choice 3 -- the kind at bit 2 and above, the choice below it.
	Wire wire;
	WriteMoverSound(wire, 5, SNAPSND_PLAT, 3);

	const std::vector<BYTE> expected = { 0x05, 0x00, 0x07 };
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(0, memcmp(wire.buf.data(), expected.data(), expected.size()));
}

TEST(MoverSoundWireBytes, GoldenMoverSoundPacksEachKindWhereItsReaderLooks)
{
	// GOLDEN for every kind the writer sends, at the choice its class can carry: a ceiling's
	// silent flag 0 to 2, a plat's type 0 to 3, and nothing for an elevator or a pillar.
	struct Case { int kind; int choice; BYTE packed; };
	const Case cases[] = {
		{ SNAPSND_CEILING,  0, 0x00 }, { SNAPSND_CEILING,  1, 0x01 }, { SNAPSND_CEILING, 2, 0x02 },
		{ SNAPSND_PLAT,     0, 0x04 }, { SNAPSND_PLAT,     3, 0x07 },
		{ SNAPSND_ELEVATOR, 0, 0x08 },
		{ SNAPSND_PILLAR,   0, 0x0C },
	};
	for (const Case &c : cases)
	{
		Wire wire;
		WriteMoverSound(wire, 0, c.kind, c.choice);
		ASSERT_EQ(wire.written(), 3u) << "kind " << c.kind;
		EXPECT_EQ(wire.buf[2], c.packed) << "kind " << c.kind << " choice " << c.choice;
	}
}

TEST(MoverSoundWireBytes, MoverSoundRoundTripsEveryKindAndChoice)
{
	// ROUND-TRIP: SNAPSND_KIND and SNAPSND_CHOICE recover what SNAPSND_PACK put in, for every
	// pair, which is what OnMoverSound branches on.
	for (int kind = 0; kind < SNAPSND_COUNT; ++kind)
	{
		for (int choice = 0; choice <= 3; ++choice)
		{
			Wire wire;
			WriteMoverSound(wire, 300 + kind, kind, choice);
			wire.openReader();

			const int sectorID = wire.r.ReadShort();
			const int packed = wire.r.ReadByte();
			EXPECT_EQ(sectorID, 300 + kind);
			EXPECT_EQ(SNAPSND_KIND(packed), kind) << "choice " << choice;
			EXPECT_EQ(SNAPSND_CHOICE(packed), choice) << "kind " << kind;
		}
	}
}

TEST(MoverSoundWireBytes, GoldenFloorSoundIsTheSectorAlone)
{
	// GOLDEN: two bytes and no more. A choice byte creeping in here is exactly what this pins,
	// since OnFloorSound reads the sector and then stops.
	Wire wire;
	WriteFloorSound(wire, 258);

	const std::vector<BYTE> expected = { 0x02, 0x01 };
	ASSERT_EQ(wire.written(), expected.size());
	EXPECT_EQ(0, memcmp(wire.buf.data(), expected.data(), expected.size()));

	wire.openReader();
	EXPECT_EQ(wire.r.ReadShort(), 258);
}

TEST(MoverSoundWireBytes, ThePayloadsAreThreeThreeAndTwoBytes)
{
	// The three shapes side by side: a door and a mover sound carry a byte the floor's does not.
	Wire door, mover, floorSound;
	WriteDoorSound(door, 11, 1, false);
	WriteMoverSound(mover, 11, SNAPSND_CEILING, 1);
	WriteFloorSound(floorSound, 11);

	EXPECT_EQ(door.written(), 3u);
	EXPECT_EQ(mover.written(), 3u);
	EXPECT_EQ(floorSound.written(), 2u);
}

TEST(MoverSoundWireBytes, ATruncatedPacketReadsInBoundsAndSignalsMinusOne)
{
	// ADVERSARIAL: cut before the packed byte and inside the sector; a short read is -1, and
	// ASan proves it stays in bounds. A sector cut short is -1, which the readers' `sectorID < 0`
	// guard refuses. A packed byte cut short is not checked: OnDoorSound would play choice 3 with
	// the check-sequence flag from it, as stock handlers read past a short packet, and
	// OnMoverSound matches no kind for it and plays nothing.
	{
		Wire wire;
		WriteDoorSound(wire, 42, 1, true);
		wire.openReader(2);					// the sector arrived, the choice did not
		EXPECT_EQ(wire.r.ReadShort(), 42);
		EXPECT_EQ(wire.r.ReadByte(), -1);
	}
	{
		Wire wire;
		WriteFloorSound(wire, 42);
		wire.openReader(1);					// half a sector index
		EXPECT_EQ(wire.r.ReadShort(), -1);
	}
	{
		Wire wire;
		wire.openReader(0);					// nothing at all
		EXPECT_EQ(wire.r.ReadShort(), -1);
		EXPECT_EQ(wire.r.ReadByte(), -1);
	}
}
