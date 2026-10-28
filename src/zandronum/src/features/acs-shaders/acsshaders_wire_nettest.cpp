// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC2_FUASETSHADERENABLED and SVC2_FUASETSHADERUNIFORM, the fua_ ACS shader functions a server sends its clients.
// [Doruk-Aksoy] Modelled on BYTESTREAM_s in the generator's field order (protocolspec/spec.misc.txt), since the generated dispatcher cannot be unit-linked (netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

// [Doruk-Aksoy] Writer and reader views over the same poisoned bytes, mirroring server-writes / client-reads.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 64) : buf(n, 0xCC) {
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

struct Enabled { std::string shader; bool own, enabled; int duration, elapsed; };
struct Uniform { std::string shader, uniform; bool own; int kind; int intValue; float v[4]; };

// [Doruk-Aksoy] What the generated FuaSetShaderEnabled::BuildNetCommand writes, header first.
void WriteEnabled(Wire &wire, const Enabled &c) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_FUASETSHADERENABLED);
	wire.w.WriteString(c.shader.c_str());
	wire.w.WriteBit(c.own);
	wire.w.WriteBit(c.enabled);
	wire.w.WriteVariable(c.duration);
	wire.w.WriteVariable(c.elapsed);
}

// [Doruk-Aksoy] What the generated parser reads after the two header bytes; false when it ran past the packet.
bool ReadEnabled(BYTESTREAM_s &r, Enabled &c) {
	c.shader = r.ReadString();
	c.own = r.ReadBit();
	c.enabled = r.ReadBit();
	c.duration = r.ReadVariable();
	c.elapsed = r.ReadVariable();
	return !(r.pbStream > r.pbStreamEnd);
}

// [Doruk-Aksoy] FuaSetShaderUniform: the int when kind is 0, else that many floats, as the spec's conditions write them.
void WriteUniform(Wire &wire, const Uniform &c) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_FUASETSHADERUNIFORM);
	wire.w.WriteString(c.shader.c_str());
	wire.w.WriteString(c.uniform.c_str());
	wire.w.WriteBit(c.own);
	wire.w.WriteByte(c.kind);
	if (c.kind == 0) wire.w.WriteLong(c.intValue);
	for (int i = 0; i < c.kind && i < 4; i++) wire.w.WriteFloat(c.v[i]);
}

bool ReadUniform(BYTESTREAM_s &r, Uniform &c) {
	c.shader = r.ReadString();
	c.uniform = r.ReadString();
	c.own = r.ReadBit();
	c.kind = r.ReadByte();
	if (c.kind == 0) c.intValue = r.ReadLong();
	for (int i = 0; i < c.kind && i < 4; i++) c.v[i] = r.ReadFloat();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(FuaShaderWire, AreAppendedAfterTheLastExistingCommands) {
	// [Doruk-Aksoy] The ids shipped before them keep their values, and both fit the SVC2 byte.
	EXPECT_EQ(74, SVC2_FUASETMUSICVOLUME);
	EXPECT_EQ(75, SVC2_FUASETSHADERENABLED);
	EXPECT_EQ(76, SVC2_FUASETSHADERUNIFORM);
	EXPECT_EQ(SVC2_FUASETSHADERUNIFORM + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(FuaShaderWire, GoldenEnabledForTheActivatorWithADuration) {
	Wire wire;
	WriteEnabled(wire, { "fire", true, true, 35, 0 });
	const BYTE expected[] = {
		232, 75,                    // SVC_EXTENDEDCOMMAND, SVC2_FUASETSHADERENABLED
		'f', 'i', 'r', 'e', 0,      // shader
		0x07,                       // own 1, enabled 1, the duration's length 1 (bits 1, 0), the elapsed tics' length 0
		35,                         // duration as a byte
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

// [Doruk-Aksoy] A joiner's enable: a minute left as a short, 70 tics run as a byte.
TEST(FuaShaderWire, GoldenEnabledForAJoinerWithItsClock) {
	Wire wire;
	WriteEnabled(wire, { "u2f", false, true, 2100, 70 });
	const BYTE expected[] = {
		232, 75, 'u', '2', 'f', 0,
		0x1A,                       // own 0, enabled 1, the duration's length 2 (bits 0, 1), the elapsed tics' length 1 (bits 1, 0)
		0x34, 0x08,                 // 2100 as a short
		70,                         // elapsed as a byte
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(FuaShaderWire, GoldenEnabledOffForEveryoneSendsNoDuration) {
	Wire wire;
	WriteEnabled(wire, { "vig", false, false, 0, 0 });
	const BYTE expected[] = { 232, 75, 'v', 'i', 'g', 0, 0x00 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(FuaShaderWire, GoldenAVec3ForEveryone) {
	Wire wire;
	WriteUniform(wire, { "vig", "x", false, 3, 0, { 1.0f, 0.5f, -1.5f, 0.0f } });
	const BYTE expected[] = {
		232, 76,                    // SVC_EXTENDEDCOMMAND, SVC2_FUASETSHADERUNIFORM
		'v', 'i', 'g', 0, 'x', 0,   // shader, uniform
		0x00,                       // own 0
		3,                          // kind: three floats
		0x00, 0x00, 0x80, 0x3F,     // 1.0f
		0x00, 0x00, 0x00, 0x3F,     // 0.5f
		0x00, 0x00, 0xC0, 0xBF,     // -1.5f
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(FuaShaderWire, GoldenAnIntForTheActivator) {
	Wire wire;
	WriteUniform(wire, { "m", "n", true, 0, 123456789, { 0, 0, 0, 0 } });
	const BYTE expected[] = { 232, 76, 'm', 0, 'n', 0, 0x01, 0, 0x15, 0xCD, 0x5B, 0x07 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(FuaShaderWire, EnabledRoundTripsEveryDurationWidth) {
	// [Doruk-Aksoy] None, a byte, a short and a long duration, each flag either way.
	for (int duration : { 0, 35, 700, 100000 })
		for (int own = 0; own < 2; own++) {
			Wire wire;
			const Enabled sent = { "Fire", own != 0, own == 0, duration, duration / 2 };
			WriteEnabled(wire, sent);
			wire.openReader();
			ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
			ASSERT_EQ(SVC2_FUASETSHADERENABLED, wire.r.ReadByte());
			Enabled got = { "", false, false, -1, -1 };
			ASSERT_TRUE(ReadEnabled(wire.r, got));
			EXPECT_EQ(sent.shader, got.shader);
			EXPECT_EQ(sent.own, got.own);
			EXPECT_EQ(sent.enabled, got.enabled);
			EXPECT_EQ(sent.duration, got.duration);
			EXPECT_EQ(sent.elapsed, got.elapsed);
		}
}

TEST(FuaShaderWire, UniformRoundTripsEveryKindBitForBit) {
	for (int kind = 0; kind <= 4; kind++) {
		Wire wire;
		const Uniform sent = { "pp", "u", kind % 2 == 1, kind, -7, { 0.25f, -3.5f, 1.f / 65536.f, 12345.75f } };
		WriteUniform(wire, sent);
		wire.openReader();
		ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
		ASSERT_EQ(SVC2_FUASETSHADERUNIFORM, wire.r.ReadByte());
		Uniform got = { "", "", false, -1, 0, { 0, 0, 0, 0 } };
		ASSERT_TRUE(ReadUniform(wire.r, got)) << kind;
		EXPECT_EQ(sent.kind, got.kind);
		EXPECT_EQ(sent.own, got.own);
		if (kind == 0) EXPECT_EQ(-7, got.intValue);
		for (int i = 0; i < kind; i++) EXPECT_EQ(0, std::memcmp(&got.v[i], &sent.v[i], sizeof(float))) << kind << " " << i;
	}
}

TEST(FuaShaderWire, ATruncatedPacketIsRefused) {
	// [Doruk-Aksoy] Every cut short of the whole command reads past its end, so the generated parser never runs it.
	Wire wire;
	WriteUniform(wire, { "vig", "x", false, 4, 0, { 1, 2, 3, 4 } });
	const size_t full = wire.written();
	for (size_t cut = 2; cut < full; cut++) {
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		Uniform got = { "", "", false, -1, 0, { 0, 0, 0, 0 } };
		EXPECT_FALSE(ReadUniform(wire.r, got)) << cut;
	}
	Wire enabled;
	WriteEnabled(enabled, { "fire", true, true, 700, 35 });
	const size_t whole = enabled.written();
	for (size_t cut = 2; cut < whole; cut++) {
		enabled.openReader(cut);
		enabled.r.ReadByte();
		enabled.r.ReadByte();
		Enabled got = { "", false, false, 0, 0 };
		EXPECT_FALSE(ReadEnabled(enabled.r, got)) << cut;
	}
}
