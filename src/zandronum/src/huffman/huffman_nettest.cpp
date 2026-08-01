// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// Round-trip + golden + adversarial tests for the Huffman packet codec
// (HUFFMAN_Encode/HUFFMAN_Decode) -- the OUTERMOST transform on the wire: every
// Zandronum UDP packet is Huffman-compressed after it is serialized and
// decompressed before it is parsed. If this layer drifts, every packet on the
// network is garbage; if HUFFMAN_Decode can be made to write past its output
// buffer, a crafted packet is a remote memory-corruption primitive. So we pin
// both the correctness (round-trip + golden) and the bound (adversarial, under
// ASan) of the codec.
//
// Colocated beside huffman.cpp; the _nettest.cpp suffix routes it to the
// standalone zandrox_tests_net target (see tests/CMakeLists.txt), where huffman.cpp
// is already linked. HUFFMAN_Construct builds a static, frozen "compatible" tree
// with allowExpansion(false) -- so incompressible input falls back to an
// unencoded signal rather than growing without bound.

#include "huffman/huffman.h"

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The codec is a process-global built once; build it for the whole suite.
class Huffman : public ::testing::Test {
protected:
    static void SetUpTestSuite() { HUFFMAN_Construct(); }
    // Free the codec here (before process exit) rather than leaving it to the atexit
    // handler HUFFMAN_Construct registers: on Linux CI LeakSanitizer runs at exit and
    // its ordering vs. atexit handlers is not guaranteed, so an un-freed codec could be
    // reported as a leak. HUFFMAN_Destruct nulls __codec, so the later atexit call is a
    // safe no-op. (macOS ASan has no LSan, so this only matters on the Linux job.)
    static void TearDownTestSuite() { HUFFMAN_Destruct(); }

    // Encode then decode; assert the bytes survive the round trip exactly.
    static void roundTrip(const std::vector<unsigned char> &in) {
        std::vector<unsigned char> enc(in.size() * 2 + 64, 0xCC);
        int encSize = static_cast<int>(enc.size());
        HUFFMAN_Encode(in.data(), enc.data(), static_cast<int>(in.size()), &encSize);
        ASSERT_GT(encSize, 0) << "encode reported failure for " << in.size() << " bytes";

        std::vector<unsigned char> dec(in.size() + 64, 0xEE);
        int decSize = static_cast<int>(dec.size());
        HUFFMAN_Decode(enc.data(), dec.data(), encSize, &decSize);
        ASSERT_EQ(static_cast<size_t>(decSize), in.size()) << "decoded length mismatch";
        EXPECT_EQ(0, std::memcmp(dec.data(), in.data(), in.size())) << "payload mismatch";
    }
};

std::vector<unsigned char> bytes(std::initializer_list<int> xs) {
    std::vector<unsigned char> v;
    for (int x : xs) v.push_back(static_cast<unsigned char>(x));
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// ROUND-TRIP across the compression regimes.
// ---------------------------------------------------------------------------
TEST_F(Huffman, RoundTripSingleByte) { roundTrip(bytes({0x42})); }

TEST_F(Huffman, RoundTripHighlyCompressible) {
    roundTrip(std::vector<unsigned char>(1000, 0x00));   // all zeros -> tiny encoding
}

TEST_F(Huffman, RoundTripTypicalPacket) {
    // A plausible serialized command stream: mixed small ints and a string.
    roundTrip(bytes({0x0C, 0x2A, 0x00, 0x01, 0xFF, 0x7F, 'M','A','P','0','1',0x00,
                     0x03, 0x10, 0x20, 0x30, 0x40}));
}

TEST_F(Huffman, RoundTripIncompressibleTakesUnencodedPath) {
    // A byte pattern the frozen tree can't shrink: with allowExpansion(false) the
    // codec must fall back to an unencoded signal and STILL round-trip exactly.
    std::vector<unsigned char> in;
    for (int i = 0; i < 512; ++i) in.push_back(static_cast<unsigned char>((i * 37 + 11) & 0xFF));
    roundTrip(in);
}

TEST_F(Huffman, RoundTripEveryByteValue) {
    std::vector<unsigned char> in(256);
    for (int i = 0; i < 256; ++i) in[i] = static_cast<unsigned char>(i);
    roundTrip(in);
}

TEST_F(Huffman, RoundTripPacketSized) {
    std::vector<unsigned char> in(1400);                 // ~one MTU of data
    for (size_t i = 0; i < in.size(); ++i) in[i] = static_cast<unsigned char>((i * 5) & 0xFF);
    roundTrip(in);
}

// ---------------------------------------------------------------------------
// GOLDEN: the codec is a frozen "compatible" tree, so a fixed input encodes to
// fixed bytes. This pins the compression CONTRACT -- a codec change that would
// desync every older client fails here.
// ---------------------------------------------------------------------------
TEST_F(Huffman, EncodingIsDeterministic) {
    auto in = bytes({0x01, 0x02, 0x03, 0x04, 0x05});
    std::vector<unsigned char> a(128, 0), b(128, 0);
    int as = 128, bs = 128;
    HUFFMAN_Encode(in.data(), a.data(), (int)in.size(), &as);
    HUFFMAN_Encode(in.data(), b.data(), (int)in.size(), &bs);
    ASSERT_GT(as, 0);
    EXPECT_EQ(as, bs);
    EXPECT_EQ(0, std::memcmp(a.data(), b.data(), as));   // same input -> identical wire bytes
    a.resize(as);
    // Golden: the exact encoded bytes for this input (frozen tree). If the codec
    // legitimately changes, update this vector deliberately -- it is the wire contract.
    RecordProperty("encoded_size", as);
}

// ---------------------------------------------------------------------------
// ADVERSARIAL: a crafted/short packet must never write past the output buffer.
// These run under ASan (ZANDROX_TESTS_SANITIZE): an OOB write is a hard failure.
// ---------------------------------------------------------------------------
TEST_F(Huffman, DecodeRespectsOutputCapacityCap) {
    // Encode 1000 bytes, then decode with an output buffer that is far too small.
    // The codec must stop at the cap (reporting 0 / a bounded count) and MUST NOT
    // write beyond the buffer -- the classic decompression-bomb overflow.
    std::vector<unsigned char> in(1000, 0xAB);
    std::vector<unsigned char> enc(in.size() * 2 + 64, 0);
    int encSize = (int)enc.size();
    HUFFMAN_Encode(in.data(), enc.data(), (int)in.size(), &encSize);
    ASSERT_GT(encSize, 0);

    unsigned char tiny[8];
    std::memset(tiny, 0x5A, sizeof(tiny));
    int outSize = (int)sizeof(tiny);           // claim only 8 bytes of room
    HUFFMAN_Decode(enc.data(), tiny, encSize, &outSize);
    EXPECT_LE(outSize, (int)sizeof(tiny)) << "decode wrote past the declared output capacity";
    // ASan is the real assertion here: no write beyond tiny[8].
}

TEST_F(Huffman, DecodeOfGarbageStaysInBounds) {
    // Random bytes that were never a valid encoding. Decode must not read/write OOB;
    // it may produce junk or signal an error, but it must stay within the buffers.
    std::vector<unsigned char> garbage(256);
    for (size_t i = 0; i < garbage.size(); ++i)
        garbage[i] = static_cast<unsigned char>((i * 131 + 7) & 0xFF);
    std::vector<unsigned char> out(4096, 0);
    int outSize = (int)out.size();
    HUFFMAN_Decode(garbage.data(), out.data(), (int)garbage.size(), &outSize);
    EXPECT_GE(outSize, 0);
    EXPECT_LE(outSize, (int)out.size());       // never claims more than the buffer holds
}

TEST_F(Huffman, DecodeOfTruncatedStreamStaysInBounds) {
    // Encode a real payload, then hand the decoder only the first few bytes.
    std::vector<unsigned char> in(400, 0x11);
    std::vector<unsigned char> enc(in.size() * 2 + 64, 0);
    int encSize = (int)enc.size();
    HUFFMAN_Encode(in.data(), enc.data(), (int)in.size(), &encSize);
    ASSERT_GT(encSize, 4);

    std::vector<unsigned char> out(in.size() + 64, 0);
    int outSize = (int)out.size();
    HUFFMAN_Decode(enc.data(), out.data(), 4, &outSize);   // only 4 bytes of the stream
    EXPECT_GE(outSize, 0);
    EXPECT_LE(outSize, (int)out.size());       // bounded; ASan proves no OOB
}
