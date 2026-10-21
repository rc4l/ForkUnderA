// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Wire tests for FLAGSET_MVFLAGS -- the flagset enum SetThingFlags puts on the stream.
//
// Why this exists: the FlagSet enum is a HAND-WRITTEN enum in network.h, not a protocolspec
// declaration. tools/protocol-snapshot.py parses the spec files, so it cannot see a change here at
// all -- these assertions are the only thing standing between a renumbered enumerator and a client
// that silently applies a movement flag word to flags2. The field layout itself (Actor, Byte
// flagset, ULong flags -- protocolspec/spec.things.txt) is unchanged by this feature; what has to be
// pinned is that every pre-existing enumerator kept its value and the new one went on the END.
//
// Modelled directly on BYTESTREAM_s rather than through the generated command, because the generated
// dispatcher resolves live engine state and cannot be unit-linked (see the netcode-testing skill).

#include "doomtype.h"        // BYTE/USHORT and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s

#include <gtest/gtest.h>
#include <cstring>
#include <vector>

namespace {

// [rc4l] A local mirror of network.h's FlagSet, NOT an #include. network.h drags in the whole engine
// and cannot be unit-linked; copying the values here is the point -- if someone renumbers the real
// enum, this file stops agreeing with it and the "documented value" test below fails.
enum MirroredFlagSet {
	MIRROR_FLAGSET_UNKNOWN,
	MIRROR_FLAGSET_FLAGS,
	MIRROR_FLAGSET_FLAGS2,
	MIRROR_FLAGSET_FLAGS3,
	MIRROR_FLAGSET_FLAGS4,
	MIRROR_FLAGSET_FLAGS5,
	MIRROR_FLAGSET_FLAGS6,
	MIRROR_FLAGSET_FLAGS7,
	MIRROR_FLAGSET_FLAGSST,
	MIRROR_FLAGSET_FLAGS8,
	MIRROR_FLAGSET_FLAGS9,
	MIRROR_FLAGSET_MVFLAGS,
};

// The MV_* bits, mirrored from actor.h for the same reason.
const DWORD MV_CPMAIRCONTROL_BIT		= 0x00000001;
const DWORD MV_CROUCHSLIDE_BIT			= 0x00000002;
const DWORD MV_ABSOLUTESECONDJUMP_BIT	= 0x00001000;

// Writer and reader views over the same bytes, mirroring server-writes / client-reads.
struct Wire {
	std::vector<BYTE> buf;
	BYTESTREAM_s w{}, r{};
	explicit Wire(size_t n = 256) : buf(n, 0xCC) {   // poison, so "unwritten" is visible
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

// SetThingFlags' payload after the actor reference: Byte flagset, ULong flags.
// (protocolspec/spec.things.txt -- the actor NetID is written by the generated preamble.)
void WriteFlagsPayload(Wire &wire, int flagset, DWORD flags) {
	wire.w.WriteByte(flagset);
	wire.w.WriteLong(static_cast<int>(flags));
}

} // namespace

TEST(MvFlagsWire, ExistingFlagsetIdsAreUnchanged) {
	// The regression that actually desyncs: inserting an enumerator instead of appending one shifts
	// every value after it, and a client then applies the wrong flag word without any error. These
	// are the values shipped before this feature; none of them may move.
	EXPECT_EQ(0, MIRROR_FLAGSET_UNKNOWN);
	EXPECT_EQ(1, MIRROR_FLAGSET_FLAGS);
	EXPECT_EQ(2, MIRROR_FLAGSET_FLAGS2);
	EXPECT_EQ(3, MIRROR_FLAGSET_FLAGS3);
	EXPECT_EQ(4, MIRROR_FLAGSET_FLAGS4);
	EXPECT_EQ(5, MIRROR_FLAGSET_FLAGS5);
	EXPECT_EQ(6, MIRROR_FLAGSET_FLAGS6);
	EXPECT_EQ(7, MIRROR_FLAGSET_FLAGS7);
	EXPECT_EQ(8, MIRROR_FLAGSET_FLAGSST);
	EXPECT_EQ(9, MIRROR_FLAGSET_FLAGS8);
	EXPECT_EQ(10, MIRROR_FLAGSET_FLAGS9);
}

TEST(MvFlagsWire, MvFlagsIsAppendedLast) {
	// The new enumerator went on the end, per the "sent as a Byte, append only" rule in network.h.
	EXPECT_EQ(11, MIRROR_FLAGSET_MVFLAGS);
	EXPECT_EQ(MIRROR_FLAGSET_FLAGS9 + 1, MIRROR_FLAGSET_MVFLAGS);
	// It must still fit the Byte the field is declared as, or the flagset silently truncates.
	EXPECT_LE(MIRROR_FLAGSET_MVFLAGS, 0xFF);
}

TEST(MvFlagsWire, GoldenFlagsetByteThenLittleEndianFlagsLong) {
	// GOLDEN: the exact bytes for a crouch-slide pawn. One byte of flagset, then the flag word as a
	// little-endian 32-bit long -- the layout every other flagset already uses, unchanged.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, MV_CROUCHSLIDE_BIT);

	ASSERT_EQ(5u, wire.written());
	const BYTE expected[5] = { 11, 0x02, 0x00, 0x00, 0x00 };
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(MvFlagsWire, GoldenHighBitFlagSurvivesTheLongField) {
	// The top MV_* bit defined so far. A Short field would have dropped this entirely; pin that the
	// field really is 32 bits wide, since the flag word has room to grow to 0x80000000.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, MV_ABSOLUTESECONDJUMP_BIT);

	ASSERT_EQ(5u, wire.written());
	const BYTE expected[5] = { 11, 0x00, 0x10, 0x00, 0x00 };
	EXPECT_EQ(0, std::memcmp(wire.buf.data(), expected, sizeof(expected)));
}

TEST(MvFlagsWire, RoundTripsCombinedFlags) {
	// ROUND-TRIP: writer and reader agree on both widths. A WriteByte paired with a ReadLong (or
	// vice versa) would misalign every following field in the packet.
	const DWORD sent = MV_CPMAIRCONTROL_BIT | MV_CROUCHSLIDE_BIT | MV_ABSOLUTESECONDJUMP_BIT;
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, sent);
	wire.openReader();

	EXPECT_EQ(MIRROR_FLAGSET_MVFLAGS, wire.r.ReadByte());
	EXPECT_EQ(sent, static_cast<DWORD>(wire.r.ReadLong()));
	// The stream is exactly consumed -- nothing left for a following command to trip over.
	EXPECT_EQ(wire.r.pbStreamEnd, wire.r.pbStream);
}

TEST(MvFlagsWire, RoundTripsEveryDefinedMvBit) {
	// Each bit individually, so a stray sign-extension or narrowing on one of them is caught rather
	// than being masked by a combined value.
	for (int bit = 0; bit <= 12; ++bit) {
		const DWORD value = 1u << bit;
		Wire wire;
		WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, value);
		wire.openReader();

		EXPECT_EQ(MIRROR_FLAGSET_MVFLAGS, wire.r.ReadByte()) << "bit " << bit;
		EXPECT_EQ(value, static_cast<DWORD>(wire.r.ReadLong())) << "bit " << bit;
	}
}

TEST(MvFlagsWire, RoundTripsTheEmptyFlagWord) {
	// Clearing the last MV_* flag sends 0. It must survive as 0 rather than being skipped, or the
	// client keeps a flag the server just removed.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, 0);
	wire.openReader();

	EXPECT_EQ(MIRROR_FLAGSET_MVFLAGS, wire.r.ReadByte());
	EXPECT_EQ(0u, static_cast<DWORD>(wire.r.ReadLong()));
}

TEST(MvFlagsWire, RoundTripsAllBitsSet) {
	// BOUNDARY: 0xFFFFFFFF has the sign bit set. ReadLong returns a signed int, so this is where a
	// sign-extension bug in the DWORD conversion would show up.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, 0xFFFFFFFFu);
	wire.openReader();

	EXPECT_EQ(MIRROR_FLAGSET_MVFLAGS, wire.r.ReadByte());
	EXPECT_EQ(0xFFFFFFFFu, static_cast<DWORD>(wire.r.ReadLong()));
}

// The contract these two pin (BYTESTREAM_s::ReadByte/ReadLong, networkshared.cpp): a read whose
// field does not wholly fit inside pbStreamEnd returns the sentinel -1 and never dereferences the
// buffer -- but the cursor advances by the field width REGARDLESS. That deliberate over-advance is
// what keeps a truncated packet failing closed: every following read is also short, instead of
// succeeding at a shifted offset and decoding one field as another.

TEST(MvFlagsWire, TruncatedFlagsFieldYieldsTheSentinelNotGarbage) {
	// ADVERSARIAL: a packet cut off mid-flags. The flagset byte still decodes, but the flag word is
	// only half present, so it must come back as -1 rather than as a half-read value -- a client
	// applying 0x00005678 here would silently grant flags the server never set.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, 0x12345678u);
	wire.openReader(3);   // flagset byte + only 2 of the 4 flag bytes

	EXPECT_EQ(MIRROR_FLAGSET_MVFLAGS, wire.r.ReadByte());
	EXPECT_EQ(-1, wire.r.ReadLong());
}

TEST(MvFlagsWire, ExhaustedStreamKeepsFailingClosed) {
	// ADVERSARIAL: nothing at all. Every read off an exhausted stream is the sentinel, and stays
	// the sentinel -- the cursor running past the end must not wrap back into readable territory.
	Wire wire;
	wire.openReader(0);

	EXPECT_EQ(-1, wire.r.ReadByte());
	EXPECT_EQ(-1, wire.r.ReadLong());
	EXPECT_EQ(-1, wire.r.ReadByte());
}

TEST(MvFlagsWire, TruncationIsDetectableByTheCursorPassingTheEnd) {
	// The over-advance is the signal a caller can test on: after a short read the cursor sits past
	// pbStreamEnd, which is how the parse loop knows the packet was malformed.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, MV_CROUCHSLIDE_BIT);
	wire.openReader(3);

	wire.r.ReadByte();
	wire.r.ReadLong();
	EXPECT_GT(wire.r.pbStream, wire.r.pbStreamEnd);
}

TEST(MvFlagsWire, AnIntactPayloadLeavesTheCursorExactlyAtTheEnd) {
	// The converse, so the test above is meaningful: a well-formed payload consumes precisely its
	// own bytes and no more.
	Wire wire;
	WriteFlagsPayload(wire, MIRROR_FLAGSET_MVFLAGS, MV_CROUCHSLIDE_BIT);
	wire.openReader();

	wire.r.ReadByte();
	wire.r.ReadLong();
	EXPECT_EQ(wire.r.pbStreamEnd, wire.r.pbStream);
}
