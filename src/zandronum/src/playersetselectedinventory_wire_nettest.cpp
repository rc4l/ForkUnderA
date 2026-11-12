// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Wire tests for SVC2_PLAYERSETSELECTEDINVENTORY (zandronum@6107a231590b339460158bf55adcc8f1f92cf806), in the generator's field order from protocolspec/spec.players.txt.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "wire_nettest.h"     // zx::nettest::Wire
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstring>

using zx::nettest::Wire;

namespace {

// [rc4l] What the generated PlayerSetSelectedInventory::BuildNetCommand writes: the header, the player, the item's class index, then whether to show the bar.
void WriteCommand(Wire &wire, BYTE player, short itemClass, bool showInventoryBar) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_PLAYERSETSELECTEDINVENTORY);
	wire.w.WriteByte(player);
	wire.w.WriteShort(itemClass);
	wire.w.WriteBit(showInventoryBar);
}

// [rc4l] What the generated parser does after the two header bytes, Execute only when nothing ran past the packet.
bool ReadCommand(BYTESTREAM_s &r, int &player, int &itemClass, bool &showInventoryBar) {
	player = r.ReadByte();
	itemClass = r.ReadShort();
	showInventoryBar = r.ReadBit();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(PlayerSetSelectedInventoryWire, IsAppendedAfterTheLastExistingCommand) {
	// [rc4l] 3.3 inserts it after SVC2_SETDOMINATIONPOINTSTATE, which would shift every id after it; ours goes last.
	EXPECT_EQ(79, SVC2_SETTHINGFLOORCLIP);
	EXPECT_EQ(80, SVC2_PLAYERSETSELECTEDINVENTORY);
	EXPECT_EQ(SVC2_PLAYERSETSELECTEDINVENTORY + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(PlayerSetSelectedInventoryWire, GoldenShownSelection) {
	Wire wire;
	WriteCommand(wire, 3, 0x0102, true);
	const BYTE expected[] = {
		232, 80,       // SVC_EXTENDEDCOMMAND, SVC2_PLAYERSETSELECTEDINVENTORY
		3,             // player
		0x02, 0x01,    // item class index, little-endian
		0x01,          // control byte: showInventoryBar true
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(PlayerSetSelectedInventoryWire, RoundTripKeepsEveryField) {
	// [rc4l] Both bar settings with the first and last player and class indices arrive exact.
	const int classes[] = { 0, 1, 0x7FFF };
	for (bool show : { false, true }) {
		for (int cls : classes) {
			Wire wire;
			WriteCommand(wire, 63, static_cast<short>(cls), show);
			wire.openReader();
			ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
			ASSERT_EQ(SVC2_PLAYERSETSELECTEDINVENTORY, wire.r.ReadByte());
			int player = -1, got = -1;
			bool shown = !show;
			ASSERT_TRUE(ReadCommand(wire.r, player, got, shown));
			EXPECT_EQ(63, player);
			EXPECT_EQ(cls, got);
			EXPECT_EQ(show, shown);
			EXPECT_EQ(wire.r.pbStreamEnd, wire.r.pbStream);
		}
	}
}

TEST(PlayerSetSelectedInventoryWire, TruncatedPacketIsNotExecuted) {
	for (size_t cut = 2; cut < 6; ++cut) {
		Wire wire;
		WriteCommand(wire, 1, 0x0102, true);
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		int player = 0, cls = 0;
		bool shown = false;
		EXPECT_FALSE(ReadCommand(wire.r, player, cls, shown)) << cut;
	}
}
