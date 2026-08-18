// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Golden + round-trip + adversarial tests for the ZandroX/Zandronum wire format
// primitives -- the BYTESTREAM_s read/write layer that EVERY SERVERCOMMANDS_*
// byte flows through. This is the rigid core: the 231 generated net commands are
// all compositions of these ~10 primitives plus a few field-layout conventions
// (little-endian ints, IEEE-754 floats, LSB-first bit packing, 2-bit-length
// varints, NUL-terminated strings). Pin the atoms and every command's encoding is
// pinned with them.
//
// Three kinds of assertion:
//   * GOLDEN   -- the exact bytes/bits on the wire (a refactor that shifts a field
//                 width or endianness fails here, not as a silent client desync).
//   * ROUNDTRIP-- write(x) then read() == x across boundary values.
//   * ADVERSARIAL -- a hostile/truncated packet must not read or write out of
//                 bounds. These run under ASan in CI (ZANDROX_TESTS_SANITIZE),
//                 so an OOB access is a hard failure, not a silent pass.
//
// Scope note: this covers the transport atoms, which are engine-decoupled and
// linkable standalone (via the tests/shims/net/i_system.h shim). The per-command dispatcher
// (CLIENT_ParseServerCommand) resolves actors/players/sectors against live engine
// state and cannot be unit-linked; its field LAYOUTS are covered here as pattern
// tests, and its behaviour belongs to the in-process replay harness (follow-up).

#include "doomtype.h"        // BYTE/USHORT/SDWORD and friends (must precede networkshared.h)
#include "networkshared.h"   // BYTESTREAM_s, MAX_NETWORK_STRING

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// A stream backed by a fixed buffer, split into a writer view and a reader view
// over the SAME bytes -- exactly how the engine writes on the server and reads on
// the client. Bit state starts as the ctor leaves it (bitBuffer=NULL, bitShift=-1),
// which forces EnsureBitSpace to claim a fresh byte on the first bit op.
struct Wire {
    std::vector<BYTE> buf;
    BYTESTREAM_s w{}, r{};
    explicit Wire(size_t n = 4096) : buf(n, 0xCC) {   // 0xCC poison so "unwritten" is visible
        w.pbStream = buf.data();
        w.pbStreamEnd = buf.data() + buf.size();
    }
    // Rewind a reader over exactly the bytes written so far.
    void openReader() {
        r = BYTESTREAM_s{};
        r.pbStream = buf.data();
        r.pbStreamEnd = w.pbStream;   // one past the last written byte
    }
    size_t written() const { return static_cast<size_t>(w.pbStream - buf.data()); }
    std::vector<BYTE> bytes() const { return {buf.begin(), buf.begin() + written()}; }
};

// Golden helper: assert the written bytes equal an exact sequence.
void expectBytes(const Wire &wire, std::vector<BYTE> golden) {
    EXPECT_EQ(wire.bytes(), golden);
}

} // namespace

// ---------------------------------------------------------------------------
// GOLDEN: fixed-width integers are little-endian.
// ---------------------------------------------------------------------------
TEST(WireGolden, ByteShortLongAreLittleEndian) {
    Wire wr;
    wr.w.WriteByte(0x41);
    wr.w.WriteShort(0x1234);
    wr.w.WriteLong(0x0A0B0C0D);
    expectBytes(wr, {0x41, /*short LE*/ 0x34, 0x12, /*long LE*/ 0x0D, 0x0C, 0x0B, 0x0A});
}

TEST(WireGolden, ByteTruncatesToLowEightBits) {
    Wire wr;
    wr.w.WriteByte(0x1FF);            // only the low byte survives
    expectBytes(wr, {0xFF});
}

TEST(WireGolden, ShortKeepsLowSixteenBits) {
    Wire wr;
    wr.w.WriteShort(0xABCDE);         // low 16 bits: 0xBCDE
    expectBytes(wr, {0xDE, 0xBC});
}

// ---------------------------------------------------------------------------
// GOLDEN: floats are the raw IEEE-754 bits, little-endian (via WriteLong).
// ---------------------------------------------------------------------------
TEST(WireGolden, FloatIsIeee754LittleEndian) {
    Wire wr;
    wr.w.WriteFloat(1.0f);            // 0x3F800000
    expectBytes(wr, {0x00, 0x00, 0x80, 0x3F});
}

// ---------------------------------------------------------------------------
// GOLDEN: strings are raw bytes followed by a single NUL (non-Windows path).
// ---------------------------------------------------------------------------
TEST(WireGolden, StringIsBytesThenNul) {
    Wire wr;
    wr.w.WriteString("AB");
    expectBytes(wr, {'A', 'B', 0x00});
}

TEST(WireGolden, NullStringIsASingleNul) {
    Wire wr;
    wr.w.WriteString(nullptr);
    expectBytes(wr, {0x00});
}

TEST(WireGolden, EmptyStringIsASingleNul) {
    Wire wr;
    wr.w.WriteString("");
    expectBytes(wr, {0x00});
}

// ---------------------------------------------------------------------------
// GOLDEN: bits pack LSB-first into a byte; a new byte is claimed on overflow.
// ---------------------------------------------------------------------------
TEST(WireGolden, BitsPackLsbFirst) {
    Wire wr;
    wr.w.WriteBit(true);   // bit 0
    wr.w.WriteBit(false);  // bit 1
    wr.w.WriteBit(true);   // bit 2
    expectBytes(wr, {0b00000101});
}

TEST(WireGolden, NinthBitRollsToASecondByte) {
    Wire wr;
    for (int i = 0; i < 8; ++i) wr.w.WriteBit(true);  // 0xFF
    wr.w.WriteBit(true);                              // bit 0 of a new byte
    expectBytes(wr, {0xFF, 0x01});
}

TEST(WireGolden, ShortBytePacksMaskedBitsAtShift) {
    Wire wr;
    wr.w.WriteShortByte(0x3, 2);   // bits 0-1 = 11
    wr.w.WriteShortByte(0x1, 2);   // bits 2-3 = 01  -> byte = 0b0111 = 0x07
    expectBytes(wr, {0x07});
}

TEST(WireGolden, ShortByteMasksValueToItsBitWidth) {
    Wire wr;
    wr.w.WriteShortByte(0xFF, 3);  // 3-bit field keeps only 0b111
    expectBytes(wr, {0x07});
}

// ---------------------------------------------------------------------------
// GOLDEN: variable-length ints -- a 2-bit length prefix, then 0/1/2/4 bytes.
// This is the SetThingState/MoveThing style compact encoding.
// ---------------------------------------------------------------------------
TEST(WireGolden, VariableZeroIsJustTheLengthPrefix) {
    Wire wr;
    wr.w.WriteVariable(0);         // length 0 -> two 0 bits, no value byte
    expectBytes(wr, {0x00});       // one byte holding the two prefix bits
}

TEST(WireGolden, VariableByteRangeUsesLengthOne) {
    Wire wr;
    wr.w.WriteVariable(5);         // length 1 (prefix bits: 1,0) then byte 0x05
    // prefix in byte 0: bit0=1,bit1=0 -> 0x01 ; value byte 0x05
    expectBytes(wr, {0x01, 0x05});
}

TEST(WireGolden, VariableShortRangeUsesLengthTwo) {
    Wire wr;
    wr.w.WriteVariable(0x1234);    // length 2 (prefix bits: 0,1) then short LE
    expectBytes(wr, {0x02, 0x34, 0x12});
}

TEST(WireGolden, VariableLargeUsesLengthThree) {
    Wire wr;
    wr.w.WriteVariable(0x0A0B0C0D); // length 3 (prefix bits: 1,1) then long LE
    expectBytes(wr, {0x03, 0x0D, 0x0C, 0x0B, 0x0A});
}

// ---------------------------------------------------------------------------
// GOLDEN + ROUNDTRIP + ADVERSARIAL: raw byte blocks (WriteBuffer/ReadBuffer),
// the primitive behind addBuffer.
// ---------------------------------------------------------------------------
TEST(WireGolden, BufferWritesRawBytesVerbatim) {
    Wire wr;
    const BYTE payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    wr.w.WriteBuffer(payload, sizeof(payload));
    expectBytes(wr, {0xDE, 0xAD, 0xBE, 0xEF});
}

TEST(WireRoundTrip, Buffer) {
    const BYTE payload[] = {0x01, 0x00, 0xFF, 0x7F, 0x80, 0x00};
    Wire wr; wr.w.WriteBuffer(payload, sizeof(payload)); wr.openReader();
    BYTE out[sizeof(payload)] = {0};
    wr.r.ReadBuffer(out, sizeof(payload));
    EXPECT_EQ(0, std::memcmp(out, payload, sizeof(payload)));
}

TEST(WireAdversarial, WriteBufferPastEndIsRefusedWhole) {
    Wire wr(3);
    const BYTE payload[] = {0xAA, 0xBB, 0xCC, 0xDD};   // 4 bytes into a 3-byte buffer
    wr.w.WriteBuffer(payload, sizeof(payload));
    EXPECT_EQ(wr.written(), 0u);                        // nothing written, no overflow
}

TEST(WireAdversarial, ReadBufferPastEndDoesNotCopyOrAdvance) {
    // A packet claiming more bytes than remain: ReadBuffer must not read OOB
    // (ASan-checked) and must leave the destination and the read pointer untouched.
    Wire wr; wr.w.WriteByte(0x11); wr.w.WriteByte(0x22); wr.openReader();
    BYTE out[8];
    std::memset(out, 0x5A, sizeof(out));
    BYTE *before = wr.r.pbStream;
    wr.r.ReadBuffer(out, sizeof(out));                 // wants 8, only 2 present
    EXPECT_EQ(wr.r.pbStream, before);                  // pointer not advanced
    for (BYTE b : out) EXPECT_EQ(b, 0x5A);             // destination untouched
}

// ---------------------------------------------------------------------------
// ROUNDTRIP: write then read returns the value, across boundaries.
// ---------------------------------------------------------------------------
TEST(WireRoundTrip, Byte) {
    for (int v : {0, 1, 127, 128, 255}) {
        Wire wr; wr.w.WriteByte(v); wr.openReader();
        EXPECT_EQ(wr.r.ReadByte(), v & 0xFF) << "byte " << v;
    }
}

TEST(WireRoundTrip, ShortSignedAndUnsigned) {
    // ReadShort sign-extends; values are stored as 16-bit two's complement.
    for (int v : {0, 1, -1, 32767, -32768, 0x1234}) {
        Wire wr; wr.w.WriteShort(v); wr.openReader();
        int16_t expect = static_cast<int16_t>(v & 0xFFFF);
        EXPECT_EQ(wr.r.ReadShort(), expect) << "short " << v;
    }
}

TEST(WireRoundTrip, Long) {
    for (long v : {0L, 1L, -1L, 2147483647L, (long)INT32_MIN, 0x0A0B0C0DL}) {
        Wire wr; wr.w.WriteLong(static_cast<int>(v)); wr.openReader();
        EXPECT_EQ(wr.r.ReadLong(), static_cast<int>(v)) << "long " << v;
    }
}

TEST(WireRoundTrip, Float) {
    for (float v : {0.0f, 1.0f, -1.0f, 3.14159f, 1e30f, -1e-30f}) {
        Wire wr; wr.w.WriteFloat(v); wr.openReader();
        EXPECT_EQ(wr.r.ReadFloat(), v) << "float " << v;   // exact: same bit pattern both ways
    }
}

TEST(WireRoundTrip, String) {
    for (const char *s : {"", "A", "hello world", "MAP01", "player_1234"}) {
        Wire wr; wr.w.WriteString(s); wr.openReader();
        EXPECT_STREQ(wr.r.ReadString(), s);
    }
}

TEST(WireRoundTrip, BitSequence) {
    const std::vector<bool> pattern = {1,0,1,1,0,0,0,1,1,1,0};  // spans a byte boundary
    Wire wr;
    for (bool b : pattern) wr.w.WriteBit(b);
    wr.openReader();
    for (size_t i = 0; i < pattern.size(); ++i)
        EXPECT_EQ(wr.r.ReadBit(), pattern[i]) << "bit " << i;
}

TEST(WireRoundTrip, ShortByteAllWidths) {
    for (int bits = 1; bits <= 8; ++bits) {
        int v = (1 << bits) - 1;                // all-ones in that width
        Wire wr; wr.w.WriteShortByte(v, bits); wr.openReader();
        EXPECT_EQ(wr.r.ReadShortByte(bits), v) << "width " << bits;
    }
}

TEST(WireRoundTrip, VariableAcrossAllLengthClasses) {
    for (int v : {0, 1, 200, 255, 256, 0x7FFF, -0x8000, 0x8000, 0x0A0B0C0D, -1}) {
        Wire wr; wr.w.WriteVariable(v); wr.openReader();
        EXPECT_EQ(wr.r.ReadVariable(), v) << "variable " << v;
    }
}

TEST(WireRoundTrip, InterleavedBitsAndBytes) {
    // The real packets interleave a bit-packed flags field with byte fields.
    Wire wr;
    wr.w.WriteBit(true);
    wr.w.WriteByte(0x42);          // a byte op flushes past the current bit byte
    wr.w.WriteBit(false);
    wr.w.WriteShort(0x1234);
    wr.openReader();
    EXPECT_EQ(wr.r.ReadBit(), true);
    EXPECT_EQ(wr.r.ReadByte(), 0x42);
    EXPECT_EQ(wr.r.ReadBit(), false);
    EXPECT_EQ(wr.r.ReadShort(), 0x1234);
}

// ---------------------------------------------------------------------------
// PATTERN: the field-layout conventions the generated commands use.
// ---------------------------------------------------------------------------
TEST(WirePattern, FixedPointTravelsAsAShiftedShort) {
    // MoveThing sends positions as `short(fixed >> FRACBITS)` and reads back
    // `short << FRACBITS`. Whole-map-unit coordinates survive exactly. (The test
    // uses `* FRACUNIT` rather than the engine's `<< FRACBITS` only to keep the
    // TEST itself free of signed-shift UB under UBSan; the round-trip is identical.)
    // NB: named kFracUnit, not FRACUNIT -- the engine's doomtype.h defines FRACUNIT as a macro,
    // and a local of that name expands to garbage. Exactly the kind of engine-macro collision a
    // wire test must dodge.
    constexpr int kFracUnit = 1 << 16;
    for (int units : {0, 1, -1, 100, -100, 32767, -32768}) {
        int fixed = units * kFracUnit;
        Wire wr; wr.w.WriteShort(fixed / kFracUnit); wr.openReader();
        int got = wr.r.ReadShort() * kFracUnit;
        EXPECT_EQ(got, fixed) << "units " << units;
    }
}

TEST(WirePattern, BitPackedFlagsFieldRoundTrips) {
    // e.g. MoveThing's `bits` word deciding which optional fields follow.
    struct { bool x, y, z, ang; } in{true, false, true, true}, out{};
    Wire wr;
    wr.w.WriteBit(in.x); wr.w.WriteBit(in.y); wr.w.WriteBit(in.z); wr.w.WriteBit(in.ang);
    wr.openReader();
    out.x = wr.r.ReadBit(); out.y = wr.r.ReadBit(); out.z = wr.r.ReadBit(); out.ang = wr.r.ReadBit();
    EXPECT_EQ(out.x, in.x); EXPECT_EQ(out.y, in.y);
    EXPECT_EQ(out.z, in.z); EXPECT_EQ(out.ang, in.ang);
}

// ---------------------------------------------------------------------------
// ADVERSARIAL: a truncated or hostile packet must stay in bounds.
// Under ASan (CI), any OOB read/write here is a hard failure.
// ---------------------------------------------------------------------------
TEST(WireAdversarial, ReadingPastEndReturnsSentinelAndAdvances) {
    Wire wr; wr.w.WriteByte(0x11); wr.openReader();
    EXPECT_EQ(wr.r.ReadByte(), 0x11);
    // Stream is now exhausted; further reads return -1 without reading OOB.
    BYTE *before = wr.r.pbStream;
    EXPECT_EQ(wr.r.ReadByte(), -1);
    EXPECT_EQ(wr.r.pbStream, before + 1);   // pointer advances; parsers detect via pbStream > pbStreamEnd
    EXPECT_EQ(wr.r.ReadShort(), -1);
    EXPECT_EQ(wr.r.ReadLong(), -1);
}

TEST(WireAdversarial, PartialMultiByteReadIsRefusedNotTruncated) {
    // Only 1 byte available but the client asks for a short/long: it must NOT
    // read the one byte and half-invent the rest -- it returns the sentinel.
    Wire wr; wr.w.WriteByte(0x11); wr.openReader();
    EXPECT_EQ(wr.r.ReadShort(), -1);        // needed 2, had 1
    Wire wr2; wr2.w.WriteByte(0x11); wr2.w.WriteByte(0x22); wr2.w.WriteByte(0x33); wr2.openReader();
    EXPECT_EQ(wr2.r.ReadLong(), -1);        // needed 4, had 3
}

TEST(WireAdversarial, WritingPastEndDoesNotCorruptBeyondTheBuffer) {
    // A 2-byte buffer; the third write must be dropped, not overflow.
    Wire wr(2);
    wr.w.WriteByte(0xAA);
    wr.w.WriteByte(0xBB);
    wr.w.WriteByte(0xCC);                    // overflow: guarded, dropped
    EXPECT_EQ(wr.written(), 2u);
    EXPECT_EQ(wr.buf[0], 0xAA);
    EXPECT_EQ(wr.buf[1], 0xBB);
}

TEST(WireAdversarial, MultiByteWritePastEndIsAllOrNothing) {
    Wire wr(3);
    wr.w.WriteByte(0xAA);                    // 1 used, 2 left
    wr.w.WriteLong(0x12345678);              // needs 4, only 2 left -> dropped whole
    EXPECT_EQ(wr.written(), 1u);
    EXPECT_EQ(wr.buf[0], 0xAA);
}

TEST(WireAdversarial, OverlongStringIsRejectedOnWrite) {
    Wire wr(MAX_NETWORK_STRING + 64);
    std::string tooLong(MAX_NETWORK_STRING + 1, 'x');   // one over the limit
    wr.w.WriteString(tooLong.c_str());
    EXPECT_EQ(wr.written(), 0u);             // nothing written -- refused
}

TEST(WireAdversarial, MaxLengthStringIsAccepted) {
    Wire wr(MAX_NETWORK_STRING + 64);
    std::string atLimit(MAX_NETWORK_STRING, 'x');
    wr.w.WriteString(atLimit.c_str());
    EXPECT_EQ(wr.written(), MAX_NETWORK_STRING + 1);     // bytes + NUL
}

TEST(WireAdversarial, UnterminatedStringStopsAtEndOfStream) {
    // A hostile packet: raw bytes with NO terminating NUL. ReadString must stop
    // at the end of the stream, not run off it.
    Wire wr;
    wr.w.WriteByte('h'); wr.w.WriteByte('i');   // no NUL
    wr.openReader();
    const char *s = wr.r.ReadString();
    EXPECT_STREQ(s, "hi");                       // consumed exactly the two bytes, bounded
}

TEST(WireAdversarial, OverlongIncomingStringTruncatesWithoutOverflow) {
    // A packet claiming a string far longer than MAX_NETWORK_STRING. ReadString
    // truncates into its fixed buffer but keeps consuming so the rest of the
    // packet stays aligned. ASan proves the fixed buffer never overflows.
    Wire wr(MAX_NETWORK_STRING * 2);
    for (int i = 0; i < MAX_NETWORK_STRING + 500; ++i) wr.w.WriteByte('A');
    wr.w.WriteByte(0x00);
    wr.w.WriteByte(0x7F);                         // a sentinel field after the string
    wr.openReader();
    const char *s = wr.r.ReadString();
    EXPECT_EQ(std::strlen(s), static_cast<size_t>(MAX_NETWORK_STRING - 1));  // truncated, NUL-terminated
    EXPECT_EQ(wr.r.ReadByte(), 0x7F);            // and the stream stayed aligned to the next field
}

TEST(WireAdversarial, ReadingBitsPastEndFallsBackWithoutOob) {
    // Exhaust the stream, then keep reading bits. EnsureBitSpace's read path
    // falls back to a static zero byte rather than reading OOB.
    Wire wr; wr.w.WriteByte(0xFF); wr.openReader();
    (void)wr.r.ReadByte();                        // drain
    for (int i = 0; i < 32; ++i)
        EXPECT_FALSE(wr.r.ReadBit());             // all zero, no crash/OOB (ASan-checked)
}

// ---------------------------------------------------------------------------
// NETADDRESS_s wire serialization -- addresses travel the wire in the server
// browser and connect handshake. Only the pure, DNS-free paths are tested here:
// WriteToStream/ReadFromStream and Compare. LoadFromString is deliberately NOT
// tested -- on anything inet_addr rejects it calls gethostbyname (a real DNS
// lookup), which is non-deterministic and CI-hostile; its 512-byte strncpy bound
// is verified by inspection, not by a test that would hit the network.
// ---------------------------------------------------------------------------
TEST(WireAddress, GoldenIsAFamilyByteThenFourIpBytesThenLittleEndianPort) {
    // [rc4l] The leading 4 is the family. Every address on the wire now says which kind it is,
    // including v4 ones, so that a v6 address can be read back as sixteen bytes rather than the
    // reader having to guess from a length it does not know.
    Wire wr;
    NETADDRESS_s a; a.Clear();
    a.abIP[0] = 1; a.abIP[1] = 2; a.abIP[2] = 3; a.abIP[3] = 4;
    a.SetPort(0x1234);
    a.WriteToStream(&wr.w, true);
    expectBytes(wr, {4, 1, 2, 3, 4, /*port LE*/ 0x34, 0x12});
}

TEST(WireAddress, GoldenForV6IsSixThenSixteenBytes) {
    Wire wr;
    NETADDRESS_s a; a.Clear();
    a.bIsIPv6 = true;
    for (int i = 0; i < 16; ++i)
        a.abIP6[i] = static_cast<BYTE>(i + 1);
    a.SetPort(0x1234);
    a.WriteToStream(&wr.w, true);
    expectBytes(wr, {6, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0x34, 0x12});
}

TEST(WireAddress, RoundTripV6) {
    Wire wr;
    NETADDRESS_s a; a.Clear();
    a.bIsIPv6 = true;
    for (int i = 0; i < 16; ++i)
        a.abIP6[i] = static_cast<BYTE>(0xa0 + i);
    a.SetPort(10666);
    a.WriteToStream(&wr.w, true);
    wr.openReader();
    NETADDRESS_s b; b.Clear();
    b.ReadFromStream(&wr.r, true);
    EXPECT_TRUE(b.bIsIPv6);
    EXPECT_TRUE(b.Compare(a)) << "a v6 address did not survive the wire";
}

TEST(WireAddress, AV4AndAV6AddressAreNeverEqual) {
    // Two different machines, even when one host is reachable at both. Collapsing them would merge
    // two server entries into one.
    NETADDRESS_s v4; v4.Clear();
    v4.abIP[0] = 1; v4.abIP[1] = 2; v4.abIP[2] = 3; v4.abIP[3] = 4;

    NETADDRESS_s v6; v6.Clear();
    v6.bIsIPv6 = true;
    v6.abIP6[15] = 1;

    EXPECT_FALSE(v4.Compare(v6));
    EXPECT_FALSE(v6.Compare(v4));
    EXPECT_FALSE(v4.CompareNoPort(v6));
}

TEST(WireAddress, AnEmptyV6AddressIsNotSet) {
    NETADDRESS_s a; a.Clear();
    a.bIsIPv6 = true;
    EXPECT_FALSE(a.IsSet());

    a.abIP6[7] = 1;
    EXPECT_TRUE(a.IsSet()) << "a nonzero byte anywhere in the sixteen counts";
}

TEST(WireAddress, V6TextIsBracketedSoThePortCanBeFound) {
    // "::1:10666" cannot be split back into an address and a port, because the address is colons
    // too. RFC 3986 brackets are what make it parseable.
    NETADDRESS_s a; a.Clear();
    a.bIsIPv6 = true;
    a.abIP6[15] = 1;
    a.SetPort(10666);

    // Plain C strings: this target links networkshared.cpp and the i_system shim, not FString.
    const char *text = a.ToString();
    EXPECT_EQ('[', text[0]) << "got: " << text;
    EXPECT_TRUE(strstr(text, "]:10666") != NULL) << "got: " << text;
}

TEST(WireAddress, V6LiteralsParseBracketedAndBare) {
    NETADDRESS_s bracketed;
    ASSERT_TRUE(bracketed.LoadFromString("[::1]:10666"));
    EXPECT_TRUE(bracketed.bIsIPv6);
    EXPECT_EQ(1, bracketed.abIP6[15]);

    NETADDRESS_s bare;
    ASSERT_TRUE(bare.LoadFromString("::1"));
    EXPECT_TRUE(bare.bIsIPv6) << "a bare literal is what somebody types when not thinking of ports";
    EXPECT_EQ(1, bare.abIP6[15]);
}

TEST(WireAddress, AV4StringIsStillReadAsV4) {
    // The v6 branch must not swallow the ordinary case: it splits on the FIRST colon to find a port,
    // and a dotted quad has exactly one.
    NETADDRESS_s a;
    ASSERT_TRUE(a.LoadFromString("192.168.0.42:10666"));
    EXPECT_FALSE(a.bIsIPv6);
    EXPECT_EQ(192, a.abIP[0]);
    EXPECT_EQ(42, a.abIP[3]);
}

TEST(WireAddress, RoundTripWithPort) {
    Wire wr;
    NETADDRESS_s a; a.Clear();
    a.abIP[0] = 192; a.abIP[1] = 168; a.abIP[2] = 0; a.abIP[3] = 42;
    a.SetPort(10666);
    a.WriteToStream(&wr.w, true);
    wr.openReader();
    NETADDRESS_s b; b.Clear();
    b.ReadFromStream(&wr.r, true);
    EXPECT_TRUE(b.Compare(a)) << "address+port did not survive the wire";
}

TEST(WireAddress, RoundTripWithoutPortLeavesPortUntouched) {
    Wire wr;
    NETADDRESS_s a; a.Clear();
    a.abIP[0] = 10; a.abIP[1] = 0; a.abIP[2] = 0; a.abIP[3] = 1;
    a.WriteToStream(&wr.w, false);          // family byte + IP, no port
    EXPECT_EQ(wr.written(), 5u);
    wr.openReader();
    NETADDRESS_s b; b.Clear();
    b.ReadFromStream(&wr.r, false);
    EXPECT_TRUE(b.CompareNoPort(a));
}

TEST(WireAddress, ComparePortSensitivity) {
    NETADDRESS_s a; a.Clear(); a.abIP[0]=1; a.abIP[1]=1; a.abIP[2]=1; a.abIP[3]=1; a.SetPort(1000);
    NETADDRESS_s b = a; b.SetPort(2000);
    EXPECT_FALSE(a.Compare(b));             // different port -> not equal
    EXPECT_TRUE(a.CompareNoPort(b));        // ... but same host
}

// --- port byte order, and v4 peers on a v6 socket --------------------------------------------------
//
// [rc4l] These exist because of a bug that was invisible to every test we had and could not be seen
// in the field either.
//
// usPort is stored in NETWORK order while SetPort converts INTO network order, so feeding one to the
// other swapped it twice: the registry's 15300 (0x3bc4) went out as 0xc43b, 50235. Every IPv6
// announce this engine ever sent went to a port nothing was listening on, and a datagram into the
// void reports success at the sender and leaves no trace at the receiver.
//
// They run on Windows, macOS and Linux in the ordinary test job, which is the point: byte order is
// exactly where the three platforms are most likely to differ, and none of this needs a network, a
// firewall or a GPU. The v4-mapped half of the same story is covered by v6mapped_compute, which can
// hold the byte pattern without dragging socket headers into a shimmed test target.

TEST(AddressPort, APortSurvivesBeingSetAndPrinted) {
    // The assertion that would have caught it: what you set is what goes out.
    NETADDRESS_s a{};
    a.LoadFromString("1.2.3.4");
    a.SetPort(15300);

    EXPECT_NE(std::string::npos, std::string(a.ToString()).find(":15300"))
        << "got " << a.ToString() << " -- a doubly byte-swapped 15300 reads as 50235";
}

TEST(AddressPort, TheStoredFieldIsNetworkOrderSoSetPortMustNotBeFedItBack) {
    // Pins the trap directly.
    NETADDRESS_s a{};
    a.SetPort(15300);

    NETADDRESS_s doubled{};
    doubled.SetPort(a.usPort);

    EXPECT_NE(a.usPort, doubled.usPort)
        << "if these are equal the double swap is a no-op and this test cannot protect anything";

    NETADDRESS_s copied{};
    copied.usPort = a.usPort;
    EXPECT_EQ(a.usPort, copied.usPort) << "copying the field is the correct way to carry a port over";
}

TEST(AddressPort, EveryPortRoundTripsThroughSetPortAndBack) {
    // The whole range rather than one value, because a byte-swap is invisible for a palindrome
    // like 0x3c3c and this class of bug hides in exactly that gap.
    const USHORT ports[] = { 1, 80, 10666, 10667, 15300, 27015, 49152, 65535 };

    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i) {
        NETADDRESS_s a{};
        a.LoadFromString("1.2.3.4");
        a.SetPort(ports[i]);

        char expected[16];
        snprintf(expected, sizeof(expected), ":%u", static_cast<unsigned>(ports[i]));
        EXPECT_NE(std::string::npos, std::string(a.ToString()).find(expected))
            << "port " << ports[i] << " printed as " << a.ToString();
    }
}
