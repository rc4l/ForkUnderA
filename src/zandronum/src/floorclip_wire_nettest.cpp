// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Wire tests for SVC2_SETTHINGFLOORCLIP (zandronum@7e0d5383e6ab), modelled on the generator's field order in protocolspec/spec.things.txt.

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s
#include "network_enums.h"   // SVC_EXTENDEDCOMMAND, SVC2_*

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

namespace {

// [rc4l] Writer and reader views over the same poisoned bytes, mirroring server-writes / client-reads.
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

// [rc4l] What the generated SetThingFloorClip::BuildNetCommand writes: the header, the actor's net id, then the clip as a 32-bit fixed.
void WriteCommand(Wire &wire, unsigned short netid, int floorclip) {
	wire.w.WriteByte(SVC_EXTENDEDCOMMAND);
	wire.w.WriteByte(SVC2_SETTHINGFLOORCLIP);
	wire.w.WriteShort(netid);
	wire.w.WriteLong(floorclip);
}

// [rc4l] What the generated parser does after the two header bytes, Execute only when nothing ran past the packet.
bool ReadCommand(BYTESTREAM_s &r, unsigned short &netid, int &floorclip) {
	netid = static_cast<unsigned short>(r.ReadShort());
	floorclip = r.ReadLong();
	return !(r.pbStream > r.pbStreamEnd);
}

} // namespace

TEST(SetThingFloorClipWire, IsAppendedAfterTheLastExistingCommand) {
	// [rc4l] 3.3 inserts it after SVC2_GIVEINVENTORYEXTRA, which would shift every id after it; ours goes last.
	EXPECT_EQ(SVC2_FUASETSHADERUNIFORM + 1, SVC2_SETTHINGFLOORCLIP);
	EXPECT_EQ(SVC2_SETTHINGFLOORCLIP + 1, NUM_SVC2_COMMANDS);
	EXPECT_LE(NUM_SVC2_COMMANDS, 0x100);
}

TEST(SetThingFloorClipWire, GoldenTenUnitClip) {
	Wire wire;
	WriteCommand(wire, 0x1234, 10 << 16);
	const BYTE expected[] = {
		232, static_cast<BYTE>(SVC2_SETTHINGFLOORCLIP),  // SVC_EXTENDEDCOMMAND, SVC2_SETTHINGFLOORCLIP
		0x34, 0x12,                                      // net id, little-endian
		0x00, 0x00, 0x0A, 0x00,                          // 10.0 in 16.16, little-endian
	};
	ASSERT_EQ(sizeof(expected), wire.written());
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(SetThingFloorClipWire, RoundTripKeepsTheClip) {
	// [rc4l] No clip, a fraction, a deep clip, a negative one and the highest net id all arrive exact.
	const int clips[] = { 0, 1, 1 << 15, 10 << 16, 300 << 16, -(4 << 16) };
	for (int clip : clips) {
		Wire wire;
		WriteCommand(wire, 0xFFFF, clip);
		wire.openReader();
		ASSERT_EQ(SVC_EXTENDEDCOMMAND, wire.r.ReadByte());
		ASSERT_EQ(SVC2_SETTHINGFLOORCLIP, wire.r.ReadByte());
		unsigned short netid = 0;
		int got = -1;
		ASSERT_TRUE(ReadCommand(wire.r, netid, got));
		EXPECT_EQ(0xFFFF, netid);
		EXPECT_EQ(clip, got);
	}
}

TEST(SetThingFloorClipWire, TruncatedPacketIsNotExecuted) {
	for (size_t cut = 2; cut < 8; ++cut) {
		Wire wire;
		WriteCommand(wire, 7, 10 << 16);
		wire.openReader(cut);
		wire.r.ReadByte();
		wire.r.ReadByte();
		unsigned short netid = 0;
		int got = 0;
		EXPECT_FALSE(ReadCommand(wire.r, netid, got)) << cut;
	}
}
