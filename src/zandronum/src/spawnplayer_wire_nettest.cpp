// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Wire tests for SVC_SPAWNPLAYER's class number, an unsigned short since zandronum@51c804da8b73, modelled on the generator's field order in protocolspec/spec.players.txt.

#include "wire_nettest.h"
#include "network_enums.h"   // SVC_SPAWNPLAYER

#include <gtest/gtest.h>
#include <cstring>

using zx::nettest::Wire;

namespace {

// [rc4l] The command's fields, in spec order.
struct Payload {
	int player = 3;
	int priorState = 2;
	bool isBot = false, isSpectating = false, isDeadSpectator = false, isMorphed = false;
	unsigned short netid = 0x1234;
	int angle = 0x40000000;
	int x = 64 << 16, y = 128 << 16, z = 0;
	unsigned short playerClass = 299;
	int morphStyle = 0, morphedClass = 0;
};

// [rc4l] What the generated SpawnPlayer::BuildNetCommand writes, header first.
void WriteCommand(Wire &wire, const Payload &p) {
	wire.w.WriteByte(SVC_SPAWNPLAYER);
	wire.w.WriteByte(p.player);
	wire.w.WriteShortByte(p.priorState, 4);
	wire.w.WriteBit(p.isBot);
	wire.w.WriteBit(p.isSpectating);
	wire.w.WriteBit(p.isDeadSpectator);
	wire.w.WriteBit(p.isMorphed);
	wire.w.WriteShort(p.netid);
	wire.w.WriteLong(p.angle);
	wire.w.WriteLong(p.x);
	wire.w.WriteLong(p.y);
	wire.w.WriteLong(p.z);
	wire.w.WriteShort(p.playerClass);
	if (p.isMorphed) {
		wire.w.WriteShort(p.morphStyle);
		wire.w.WriteShort(p.morphedClass);
	}
}

// [rc4l] What the generated parser does after the header byte, Execute only when nothing ran past the packet.
bool ReadCommand(BYTESTREAM_s &r, Payload &p) {
	p.player = r.ReadByte();
	p.priorState = r.ReadShortByte(4);
	p.isBot = r.ReadBit();
	p.isSpectating = r.ReadBit();
	p.isDeadSpectator = r.ReadBit();
	p.isMorphed = r.ReadBit();
	p.netid = static_cast<unsigned short>(r.ReadShort());
	p.angle = r.ReadLong();
	p.x = r.ReadLong();
	p.y = r.ReadLong();
	p.z = r.ReadLong();
	p.playerClass = static_cast<unsigned short>(r.ReadShort());
	if (p.isMorphed) {
		p.morphStyle = r.ReadShort();
		p.morphedClass = r.ReadShort();
	}
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(SpawnPlayerWire, GoldenClassPastAByte) {
	Wire wire;
	WriteCommand(wire, Payload{});
	const BYTE expected[] = {
		static_cast<BYTE>(SVC_SPAWNPLAYER), 3,   // header, player
		0x02,                                    // prior state 2 in the low four bits, no flags
		0x34, 0x12,                              // net id, little-endian
		0x00, 0x00, 0x00, 0x40,                  // ANG90
		0x00, 0x00, 0x40, 0x00,                  // x 64.0
		0x00, 0x00, 0x80, 0x00,                  // y 128.0
		0x00, 0x00, 0x00, 0x00,                  // z 0
		0x2B, 0x01,                              // class 299, little-endian
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SpawnPlayerWire, RoundTripKeepsEveryClassNumber) {
	// [rc4l] A byte kept 299 as 43 and 256 as 0; the short keeps them, up to the last it can number.
	const unsigned short classes[] = { 0, 1, 255, 256, 299, 0xFFFF };
	for (unsigned short cls : classes) {
		Payload sent;
		sent.playerClass = cls;
		Wire wire;
		WriteCommand(wire, sent);
		wire.openReader();
		ASSERT_EQ(SVC_SPAWNPLAYER, wire.r.ReadByte());
		Payload got;
		got.playerClass = 0;
		ASSERT_TRUE(ReadCommand(wire.r, got));
		EXPECT_EQ(cls, got.playerClass);
		EXPECT_EQ(sent.netid, got.netid);
		EXPECT_EQ(sent.z, got.z);
	}
}

TEST(SpawnPlayerWire, MorphTailFollowsTheWiderClass) {
	Payload sent;
	sent.isMorphed = true;
	sent.isSpectating = true;
	sent.morphStyle = 0x21;
	sent.morphedClass = 0x0456;
	Wire wire;
	WriteCommand(wire, sent);
	EXPECT_EQ(0x02 | 0x20 | 0x80, wire.buf[2]);
	wire.openReader();
	wire.r.ReadByte();
	Payload got;
	ASSERT_TRUE(ReadCommand(wire.r, got));
	EXPECT_TRUE(got.isMorphed);
	EXPECT_TRUE(got.isSpectating);
	EXPECT_EQ(299, got.playerClass);
	EXPECT_EQ(0x21, got.morphStyle);
	EXPECT_EQ(0x0456, got.morphedClass);
}

TEST(SpawnPlayerWire, TruncatedPacketIsNotExecuted) {
	Wire full;
	WriteCommand(full, Payload{});
	const size_t length = full.written();
	for (size_t cut = 1; cut < length; ++cut) {
		Wire wire;
		WriteCommand(wire, Payload{});
		wire.openReader(cut);
		wire.r.ReadByte();
		Payload got;
		EXPECT_FALSE(ReadCommand(wire.r, got)) << cut;
	}
}
