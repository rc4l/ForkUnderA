// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Wire tests for SVC2_FUASETMUSICVOLUME, the music volume factor ACS SetMusicVolume sets on a server.
// [Doruk-Aksoy] Modelled on BYTESTREAM_s in the generator's field order (protocolspec/spec.misc.txt), since the generated dispatcher cannot be unit-linked (netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

namespace {

// [Doruk-Aksoy] Writer and reader views over the same poisoned bytes, mirroring server-writes / client-reads.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 32) : buf(n, 0xCC) {
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

// [Doruk-Aksoy] What the generated FuaSetMusicVolume::BuildNetCommand writes, header first.
void WriteCommand(Wire &wire, float volume) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_FUASETMUSICVOLUME);
	wire.w.WriteFloat(volume);
}

// [Doruk-Aksoy] What the generated parser does after the two header bytes: the volume, then Execute only when nothing ran past the packet.
bool ReadCommand(BYTESTREAM_s &r, float &volume) {
	volume = r.ReadFloat();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(FuaSetMusicVolumeWire, IsAppendedAfterTheLastExistingCommand) {
	// [Doruk-Aksoy] The ids shipped before it keep their values, and it fits the SVC2 byte.
	EXPECT_EQ(73, SVC2_FUADAMAGETAKEN);
	EXPECT_EQ(74, SVC2_FUASETMUSICVOLUME);
	EXPECT_EQ(SVC2_FUASETMUSICVOLUME + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(FuaSetMusicVolumeWire, GoldenHalfVolume) {
	Wire wire;
	WriteCommand(wire, 0.5f);
	const BYTE expected[] = {
		232, 74,                    // SVC_EXTENDEDCOMMAND, SVC2_FUASETMUSICVOLUME
		0x00, 0x00, 0x00, 0x3F,     // 0.5f, its bits little-endian
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(FuaSetMusicVolumeWire, RoundTripKeepsTheExactFactor) {
	// [Doruk-Aksoy] Silence, the default, the 2.0 ceiling and an ACS fixed-point step all arrive bit for bit.
	for (float v : { 0.f, 1.f, 2.f, 0.25f, 1.f / 65536.f, 3.5f }) {
		Wire wire;
		WriteCommand(wire, v);
		wire.openReader();
		ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
		ASSERT_EQ(SVC2_FUASETMUSICVOLUME, wire.r.ReadByte());
		float got = -1.f;
		ASSERT_TRUE(ReadCommand(wire.r, got));
		EXPECT_EQ(0, std::memcmp(&got, &v, sizeof(v))) << v;
	}
}

TEST(FuaSetMusicVolumeWire, TruncatedPacketIsNotExecuted) {
	// [Doruk-Aksoy] A volume cut short reads as the bits of -1, a NaN, which the overrun check keeps from ever being applied.
	for (size_t cut = 2; cut < 6; ++cut) {
		Wire wire;
		WriteCommand(wire, 0.5f);
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		float got = 0.f;
		EXPECT_FALSE(ReadCommand(wire.r, got)) << cut;
	}
}
