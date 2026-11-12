// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC2_FUASETACTORMATERIAL and SVC2_FUASETACTORUNIFORM, an actor's fua_Material and its own uniforms as a server sends them.
// [Doruk-Aksoy] Modelled on BYTESTREAM_s in the generator's field order (protocolspec/spec.misc.txt), an Actor field its net id as a short.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstddef>
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

struct Material { unsigned short netid; std::string material; };
struct Uniform { unsigned short netid; int uniform, kind, intValue; float v[4]; };

// [Doruk-Aksoy] What the generated FuaSetActorMaterial::BuildNetCommand writes, header first.
void WriteMaterial(Wire &wire, const Material &c) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_FUASETACTORMATERIAL);
	wire.w.WriteShort(c.netid);
	wire.w.WriteString(c.material.c_str());
}

bool ReadMaterial(BYTESTREAM_s &r, Material &c) {
	c.netid = static_cast<unsigned short>(r.ReadShort());
	c.material = r.ReadString();
	return !(r.pbStream > r.pbStreamEnd);
}

// [Doruk-Aksoy] FuaSetActorUniform: the uniform's index, then the int when kind is 0, else that many floats.
void WriteUniform(Wire &wire, const Uniform &c) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_FUASETACTORUNIFORM);
	wire.w.WriteShort(c.netid);
	wire.w.WriteByte(c.uniform);
	wire.w.WriteByte(c.kind);
	if (c.kind == 0) wire.w.WriteLong(c.intValue);
	for (int i = 0; i < c.kind && i < 4; i++) wire.w.WriteFloat(c.v[i]);
}

bool ReadUniform(BYTESTREAM_s &r, Uniform &c) {
	c.netid = static_cast<unsigned short>(r.ReadShort());
	c.uniform = r.ReadByte();
	c.kind = r.ReadByte();
	if (c.kind == 0) c.intValue = r.ReadLong();
	for (int i = 0; i < c.kind && i < 4; i++) c.v[i] = r.ReadFloat();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(FuaActorMaterialWire, AreAppendedAfterTheShaderCommands) {
	EXPECT_EQ(77, SVC2_FUASETACTORMATERIAL);
	EXPECT_EQ(78, SVC2_FUASETACTORUNIFORM);
	EXPECT_LT(SVC2_FUASETACTORUNIFORM, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(FuaActorMaterialWire, GoldenMaterialByName) {
	Wire wire;
	WriteMaterial(wire, { 0x1234, "glow" });
	const BYTE expected[] = { 232, 77, 0x34, 0x12, 'g', 'l', 'o', 'w', 0 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, memcmp(expected, wire.buf.data(), sizeof(expected)));
	wire.openReader();
	wire.r.ReadByte();
	wire.r.ReadByte();
	Material back{};
	ASSERT_TRUE(ReadMaterial(wire.r, back));
	EXPECT_EQ(0x1234, back.netid);
	EXPECT_EQ("glow", back.material);
}

TEST(FuaActorMaterialWire, NoneTravelsAsAnEmptyName) {
	Wire wire;
	WriteMaterial(wire, { 7, "" });
	const BYTE expected[] = { 232, 77, 7, 0, 0 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, memcmp(expected, wire.buf.data(), sizeof(expected)));
}

TEST(FuaActorMaterialWire, GoldenAnInt) {
	Wire wire;
	WriteUniform(wire, { 9, 1, 0, 3, {} });
	const BYTE expected[] = { 232, 78, 9, 0, 1, 0, 3, 0, 0, 0 };
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, memcmp(expected, wire.buf.data(), sizeof(expected)));
}

TEST(FuaActorMaterialWire, UniformRoundTripsEveryKindBitForBit) {
	for (int kind = 0; kind <= 4; kind++) {
		Wire wire;
		const Uniform sent = { 0xBEEF, 7, kind, -42, { 0.25f, -1.5f, 3.0e-7f, 1.f } };
		WriteUniform(wire, sent);
		wire.openReader();
		wire.r.ReadByte();
		wire.r.ReadByte();
		Uniform back{};
		ASSERT_TRUE(ReadUniform(wire.r, back)) << kind;
		EXPECT_EQ(sent.netid, back.netid);
		EXPECT_EQ(sent.uniform, back.uniform);
		EXPECT_EQ(kind, back.kind);
		if (kind == 0) EXPECT_EQ(-42, back.intValue);
		for (int i = 0; i < kind; i++) EXPECT_EQ(0, memcmp(&sent.v[i], &back.v[i], sizeof(float))) << kind << " " << i;
	}
}

TEST(FuaActorMaterialWire, ATruncatedPacketIsRefused) {
	Wire wire;
	WriteUniform(wire, { 1, 0, 4, 0, { 1.f, 2.f, 3.f, 4.f } });
	const size_t whole = wire.written();
	for (size_t cut = 2; cut < whole; cut++) {
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		Uniform back{};
		EXPECT_FALSE(ReadUniform(wire.r, back)) << cut;
	}
	Wire named;
	WriteMaterial(named, { 1, "glow" });
	named.openReader(named.written() - 1);
	named.r.ReadByte();
	named.r.ReadByte();
	Material m{};
	EXPECT_FALSE(ReadMaterial(named.r, m));
}
