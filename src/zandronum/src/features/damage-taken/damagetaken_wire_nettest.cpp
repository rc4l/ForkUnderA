// [MobiusU9B] Wire tests for SVC2_FUADAMAGETAKEN, the one-roll report fua_damagetaken's server sends to the player who took it.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
//
// [MobiusU9B] Modelled on BYTESTREAM_s in the generator's field order, since the generated dispatcher cannot be unit-linked (netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

// [MobiusU9B] Mirrors doomdef.h; the "attacker is not a player" value of attackerPlayer.
const int kMaxPlayers = 64;

// [MobiusU9B] Mirrors doomdef.h's TELEFRAG_DAMAGE, the largest damage a roll carries.
const int kTelefragDamage = 1000000;

// [MobiusU9B] What a null class goes on the wire as (KillPlayer's weaponType does the same).
const int kNullClass = -1;

// [MobiusU9B] The command's fields, in protocolspec/spec.misc.txt order.
struct Payload {
	int mapTime;
	int damage;
	int armorAbsorbed;
	int healthLost;
	int attackerPlayer;
	int attackerClass;
	int weaponClass;
	std::string damageType;
	bool lethal;
};

// [MobiusU9B] Writer and reader views over the same poisoned bytes, mirroring server-writes / client-reads.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 256) : buf(n, 0xCC) {
		w.pbStream = buf.data();
		w.pbStreamEnd = buf.data() + buf.size();
	}
	size_t written() const { return static_cast<size_t>(w.pbStream - buf.data()); }
	void openReader(size_t truncateTo = static_cast<size_t>(-1)) {
		const size_t n = (truncateTo == static_cast<size_t>(-1)) ? written() : truncateTo;
		r = BYTESTREAM_s{};
		r.pbStream = buf.data();
		r.pbStreamEnd = buf.data() + n;
	}
};

// [MobiusU9B] What the generated FuaDamageTaken::BuildNetCommand writes, header first.
void WriteCommand(Wire &wire, const Payload &p) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_FUADAMAGETAKEN);
	wire.w.WriteVariable(p.mapTime);
	wire.w.WriteVariable(p.damage);
	wire.w.WriteVariable(p.armorAbsorbed);
	wire.w.WriteVariable(p.healthLost);
	wire.w.WriteByte(p.attackerPlayer);
	wire.w.WriteShort(p.attackerClass);
	wire.w.WriteShort(p.weaponClass);
	wire.w.WriteString(p.damageType.c_str());
	wire.w.WriteBit(p.lethal);
}

// [MobiusU9B] What the generated parser reads after the client has consumed the two header bytes.
Payload ReadPayload(BYTESTREAM_s &r) {
	Payload p;
	p.mapTime = r.ReadVariable();
	p.damage = r.ReadVariable();
	p.armorAbsorbed = r.ReadVariable();
	p.healthLost = r.ReadVariable();
	p.attackerPlayer = r.ReadByte();
	p.attackerClass = r.ReadShort();
	p.weaponClass = r.ReadShort();
	p.damageType = r.ReadString();
	p.lethal = r.ReadBit();
	return p;
}

// [MobiusU9B] A shotgun-guy pellet: 15 damage, no armor, at 0:42.7 into the map.
Payload Pellet() {
	Payload p = { 1497, 15, 0, 15, kMaxPlayers, 0x0102, kNullClass, "None", false };
	return p;
}

} // namespace

TEST(DamageTakenWire, IsAppendedAfterTheLastExistingCommand) {
	// [MobiusU9B] The ids shipped before this command keep their values, and the new one fits the SVC2 byte.
	EXPECT_EQ(72, SVC2_SETTHINGSPRITEORIENTATION);
	EXPECT_EQ(73, SVC2_FUADAMAGETAKEN);
	EXPECT_EQ(SVC2_FUADAMAGETAKEN + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(DamageTakenWire, GoldenPellet) {
	Wire wire;
	WriteCommand(wire, Pellet());

	// [MobiusU9B] Control byte 0x46 holds the four Variable lengths (short, byte, none, byte); lethal opens a second one.
	const BYTE expected[] = {
		232, 73,                    // SVC_EXTENDEDCOMMAND, SVC2_FUADAMAGETAKEN
		0x46,                       // control byte: 2, 1, 0, 1
		0xD9, 0x05,                 // mapTime 1497
		15,                         // damage
		15,                         // healthLost (armorAbsorbed 0 sends no bytes)
		64,                         // attackerPlayer: none
		0x02, 0x01,                 // attackerClass
		0xFF, 0xFF,                 // weaponClass: none
		'N', 'o', 'n', 'e', 0,      // damageType
		0x00,                       // control byte: lethal false
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(DamageTakenWire, GoldenFatalTelefrag) {
	Payload p = { 0, kTelefragDamage, 300, 100, 3, 0x0010, 0x0011, "Telefrag", true };
	Wire wire;
	WriteCommand(wire, p);

	// [MobiusU9B] Lengths none, long, short, byte set bits 2-3, 5 and 6 of the first control byte: 0x0C | 0x20 | 0x40.
	const BYTE expected[] = {
		232, 73,                    // SVC_EXTENDEDCOMMAND, SVC2_FUADAMAGETAKEN
		0x6C,                       // control byte: 0, 3, 2, 1
		0x40, 0x42, 0x0F, 0x00,     // damage 1000000 (mapTime 0 sends no bytes)
		0x2C, 0x01,                 // armorAbsorbed 300
		100,                        // healthLost
		3,                          // attackerPlayer
		0x10, 0x00,                 // attackerClass
		0x11, 0x00,                 // weaponClass
		'T', 'e', 'l', 'e', 'f', 'r', 'a', 'g', 0,
		0x01,                       // control byte: lethal true
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(DamageTakenWire, RoundTripsAPellet) {
	Wire wire;
	WriteCommand(wire, Pellet());
	wire.openReader();

	EXPECT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
	EXPECT_EQ(SVC2_FUADAMAGETAKEN, wire.r.ReadByte());
	const Payload p = ReadPayload(wire.r);
	EXPECT_EQ(1497, p.mapTime);
	EXPECT_EQ(15, p.damage);
	EXPECT_EQ(0, p.armorAbsorbed);
	EXPECT_EQ(15, p.healthLost);
	EXPECT_EQ(kMaxPlayers, p.attackerPlayer);
	EXPECT_EQ(0x0102, p.attackerClass);
	EXPECT_EQ(0xFFFF, static_cast<USHORT>(p.weaponClass));
	EXPECT_EQ("None", p.damageType);
	EXPECT_FALSE(p.lethal);
	// [MobiusU9B] Exactly consumed, so the next command in the packet starts in the right place.
	EXPECT_EQ(wire.r.pbStreamEnd, wire.r.pbStream);
}

TEST(DamageTakenWire, RoundTripsEveryVariableWidthAndSign) {
	// [MobiusU9B] One value per Variable length, plus the extremes a roll can carry, in every damage field.
	const int values[] = { 0, 1, 255, 256, -1, 0x7FFF, -0x8000, 0x8000, kTelefragDamage, -kTelefragDamage };
	for (int value : values) {
		Payload sent = { value, value, value, value, 0, kNullClass, kNullClass, "", true };
		Wire wire;
		WriteCommand(wire, sent);
		wire.openReader();
		wire.r.ReadByte();
		wire.r.ReadByte();

		const Payload got = ReadPayload(wire.r);
		EXPECT_EQ(value, got.mapTime) << value;
		EXPECT_EQ(value, got.damage) << value;
		EXPECT_EQ(value, got.armorAbsorbed) << value;
		EXPECT_EQ(value, got.healthLost) << value;
		EXPECT_EQ("", got.damageType) << value;
		EXPECT_TRUE(got.lethal) << value;
		EXPECT_EQ(wire.r.pbStreamEnd, wire.r.pbStream) << value;
	}
}

TEST(DamageTakenWire, EveryTruncationIsCaughtByTheParsersEndCheck) {
	// [MobiusU9B] The generated parser drops the command when the cursor ends past pbStreamEnd; every cut must get there.
	Payload p = { 0, kTelefragDamage, 300, 100, 3, 0x0010, 0x0011, "Telefrag", true };
	Wire full;
	WriteCommand(full, p);
	const size_t size = full.written();

	for (size_t cut = 2; cut < size; ++cut) {
		Wire wire;
		WriteCommand(wire, p);
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		ReadPayload(wire.r);
		EXPECT_GT(wire.r.pbStream, wire.r.pbStreamEnd) << "cut at " << cut << " of " << size;
	}
}
