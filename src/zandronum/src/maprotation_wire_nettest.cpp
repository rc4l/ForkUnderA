// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Wire tests for SVC2_UPDATEMAPROTATION's used status (zandronum@f522876fe386), written as SERVERCOMMANDS_SetMapUsedStatus writes it and read as cl_main.cpp reads it.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "wire_nettest.h"     // zx::nettest::Wire
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_UPDATEMAPROTATION

#include <gtest/gtest.h>
#include <cstring>

using zx::nettest::Wire;

namespace {

// [rc4l] UPDATE_MAPROTATION_SETNEXTPOSITION and SETUSEDSTATUS in network.h, which sv_commands.cpp pins with a static_assert.
const int kSetNextPosition = 4, kSetUsedStatus = 5;

// [rc4l] The header, the update type, the entry's position, then its used status as a bit.
void WriteCommand(Wire &wire, int type, unsigned short position, bool flag) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_UPDATEMAPROTATION);
	wire.w.WriteByte(type);
	wire.w.WriteShort(position);
	wire.w.WriteBit(flag);
}

// [rc4l] What the client does after the header and type, applying it only when nothing ran past the packet.
bool ReadCommand(BYTESTREAM_s &r, unsigned &position, bool &flag) {
	position = static_cast<unsigned short>(r.ReadShort());
	flag = r.ReadBit();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(MapUsedStatusWire, GoldenThirdEntryUsed) {
	Wire wire;
	WriteCommand(wire, kSetUsedStatus, 2, true);
	const BYTE expected[] = {
		232, static_cast<BYTE>(SVC2_UPDATEMAPROTATION),  // SVC_EXTENDEDCOMMAND, SVC2_UPDATEMAPROTATION
		kSetUsedStatus,                                  // UPDATE_MAPROTATION_SETUSEDSTATUS
		0x02, 0x00,                                      // position, little-endian
		0x01,                                            // the used bit, in a byte of its own
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(MapUsedStatusWire, ReadsAsTheNextPositionDoes) {
	// [rc4l] Both updates carry a position then a bit, which the client reads through one case.
	for (int type : { kSetNextPosition, kSetUsedStatus }) {
		for (bool flag : { false, true }) {
			Wire wire;
			WriteCommand(wire, type, 0xFFFF, flag);
			wire.openReader();
			ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
			ASSERT_EQ(SVC2_UPDATEMAPROTATION, wire.r.ReadByte());
			ASSERT_EQ(type, wire.r.ReadByte());
			unsigned position = 0;
			bool got = !flag;
			ASSERT_TRUE(ReadCommand(wire.r, position, got));
			EXPECT_EQ(0xFFFFu, position);
			EXPECT_EQ(flag, got);
		}
	}
}

TEST(MapUsedStatusWire, TruncatedPacketIsNotApplied) {
	for (size_t cut = 3; cut < 6; ++cut) {
		Wire wire;
		WriteCommand(wire, kSetUsedStatus, 7, true);
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		wire.r.ReadByte();
		unsigned position = 0;
		bool got = false;
		EXPECT_FALSE(ReadCommand(wire.r, position, got)) << cut;
	}
}
