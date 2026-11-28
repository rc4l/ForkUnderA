// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC2_SETPLAYERSOUNDCLASS, the sound class a server script gives a player (APROP_SoundClass, features/acs-parity).
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

void WriteSoundClass(Wire &wire, int player, const SentName &name) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_SETPLAYERSOUNDCLASS);
	wire.w.WriteByte(player);
	wire.w.WriteShort(name.index);
	if (name.index == -1) wire.w.WriteString(name.text.c_str());
}

// [Doruk-Aksoy] What the generated parser reads after the two header bytes, as NETWORK_ReadName does; false when it ran past the packet.
bool ReadSoundClass(BYTESTREAM_s &r, int &player, SentName &name) {
	player = r.ReadByte();
	name.index = static_cast<short>(r.ReadShort());
	name.text = name.index == -1 ? r.ReadString() : "";
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(SoundClassWire, IsAppendedAfterTheLastExistingCommand) {
	// [Doruk-Aksoy] The ids shipped before it keep their values, and it fits the SVC2 byte.
	EXPECT_EQ(92, SVC2_MOVERSOUND);
	EXPECT_EQ(93, SVC2_SETPLAYERSOUNDCLASS);
	EXPECT_EQ(SVC2_SETPLAYERSOUNDCLASS + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(SoundClassWire, GoldenAScriptsOwnClass) {
	Wire wire;
	WriteSoundClass(wire, 3, { -1, "zxmarine" });
	const BYTE expected[] = {
		232, 93,                                    // SVC_EXTENDEDCOMMAND, SVC2_SETPLAYERSOUNDCLASS
		3,                                          // the player
		0xFF, 0xFF,                                 // no predefined name
		'z', 'x', 'm', 'a', 'r', 'i', 'n', 'e', 0,  // its text
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SoundClassWire, GoldenNoneGivesTheClassBackInTwoBytes) {
	// [Doruk-Aksoy] NAME_None is predefined name 0, so taking the class away costs no text.
	Wire wire;
	WriteSoundClass(wire, 0, { 0, "" });
	const BYTE expected[] = { 232, 93, 0, 0x00, 0x00 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SoundClassWire, RoundTripsEveryPlayerAndBothNameForms) {
	for (int player : { 0, 1, 31, 63 })
		for (const SentName &name : { SentName{ -1, "a" }, SentName{ -1, std::string(200, 'q') }, SentName{ 0, "" }, SentName{ 1234, "" } }) {
			Wire wire;
			WriteSoundClass(wire, player, name);
			wire.openReader();
			ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
			ASSERT_EQ(SVC2_SETPLAYERSOUNDCLASS, wire.r.ReadByte());
			int gotPlayer = -1;
			SentName got;
			ASSERT_TRUE(ReadSoundClass(wire.r, gotPlayer, got));
			EXPECT_EQ(player, gotPlayer);
			EXPECT_EQ(name.index, got.index);
			EXPECT_EQ(name.text, got.text);
		}
}

TEST(SoundClassWire, ATruncatedPacketIsRefused) {
	Wire full;
	WriteSoundClass(full, 2, { -1, "zxmarine" });
	// [Doruk-Aksoy] Cut before the name's short ends, the reader runs past the packet; cut inside the text, the string ends at the packet's end.
	for (size_t cut = 2; cut < 5; cut++) {
		Wire wire;
		WriteSoundClass(wire, 2, { -1, "zxmarine" });
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		int player = 0;
		SentName got;
		EXPECT_FALSE(ReadSoundClass(wire.r, player, got)) << cut;
	}
	ASSERT_GT(full.written(), 5u);
}
