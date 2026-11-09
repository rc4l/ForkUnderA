// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] Every branch of the zstd wire decisions: the level clamp, the try-zstd gate, the smart Huffman fallback, and the frame check with its documented one-in-2^32 misread.

#include "zstdwire_compute.h"
#include <gtest/gtest.h>

namespace {

using zx::nettransport::ComputeHuffmanFallback;
using zx::nettransport::ComputeIsZstdFrame;
using zx::nettransport::ComputeShouldTryZstd;
using zx::nettransport::ComputeUsedZstd;
using zx::nettransport::ComputeZstdLevelClamped;

//*****************************************************************************
//
TEST(ZstdWireCompute, LevelClampsLowToOne)
{
	EXPECT_EQ(1, ComputeZstdLevelClamped(0, 22));
	EXPECT_EQ(1, ComputeZstdLevelClamped(-5, 22));
}

TEST(ZstdWireCompute, LevelClampsHighToMax)
{
	EXPECT_EQ(22, ComputeZstdLevelClamped(23, 22));
	EXPECT_EQ(22, ComputeZstdLevelClamped(1000, 22));
}

TEST(ZstdWireCompute, LevelInRangeIsUntouched)
{
	EXPECT_EQ(1, ComputeZstdLevelClamped(1, 22));
	EXPECT_EQ(12, ComputeZstdLevelClamped(12, 22));
	EXPECT_EQ(22, ComputeZstdLevelClamped(22, 22));
}

//*****************************************************************************
//
TEST(ZstdWireCompute, TryZstdNeedsTheCallersFlag)
{
	EXPECT_FALSE(ComputeShouldTryZstd(false, 1000, 100));
}

TEST(ZstdWireCompute, TryZstdNeedsTheThreshold)
{
	// [KI] 3.3's comparison is >=, so a packet of exactly net_zstd_threshold bytes goes as zstd.
	EXPECT_FALSE(ComputeShouldTryZstd(true, 99, 100));
	EXPECT_TRUE(ComputeShouldTryZstd(true, 100, 100));
	EXPECT_TRUE(ComputeShouldTryZstd(true, 101, 100));
}

//*****************************************************************************
//
TEST(ZstdWireCompute, FallbackAlwaysWhenZstdDidNotRun)
{
	EXPECT_TRUE(ComputeHuffmanFallback(false, false, 0, 500));
	EXPECT_TRUE(ComputeHuffmanFallback(false, true, 0, 500));
}

TEST(ZstdWireCompute, NoFallbackWhenZstdShrankThePacket)
{
	EXPECT_FALSE(ComputeHuffmanFallback(true, false, 300, 500));
	EXPECT_FALSE(ComputeHuffmanFallback(true, true, 300, 500));
}

TEST(ZstdWireCompute, SmartFallsBackWhenZstdDidNotShrink)
{
	// [KI] 3.3's comparison is >=, so a zstd form exactly the input's size falls back under net_zstd_smart and stays zstd without it.
	EXPECT_TRUE(ComputeHuffmanFallback(true, true, 500, 500));
	EXPECT_TRUE(ComputeHuffmanFallback(true, true, 501, 500));
	EXPECT_FALSE(ComputeHuffmanFallback(true, false, 501, 500));
}

//*****************************************************************************
//
TEST(ZstdWireCompute, FrameCheckReadsTheMagicBytes)
{
	const unsigned char frame[] = { 0x28, 0xB5, 0x2F, 0xFD, 0x00 };
	EXPECT_TRUE(ComputeIsZstdFrame(frame, 5));
	EXPECT_TRUE(ComputeIsZstdFrame(frame, 4));
}

TEST(ZstdWireCompute, FrameCheckRejectsShortDatagrams)
{
	const unsigned char frame[] = { 0x28, 0xB5, 0x2F, 0xFD };
	EXPECT_FALSE(ComputeIsZstdFrame(frame, 3));
	EXPECT_FALSE(ComputeIsZstdFrame(frame, 0));
}

TEST(ZstdWireCompute, FrameCheckRejectsEveryOtherFirstByte)
{
	// [KI] Each of the four positions wrong in turn, so every comparison in the chain is the one that rejects once.
	const unsigned char b0[] = { 0x29, 0xB5, 0x2F, 0xFD };
	const unsigned char b1[] = { 0x28, 0xB6, 0x2F, 0xFD };
	const unsigned char b2[] = { 0x28, 0xB5, 0x30, 0xFD };
	const unsigned char b3[] = { 0x28, 0xB5, 0x2F, 0xFE };
	EXPECT_FALSE(ComputeIsZstdFrame(b0, 4));
	EXPECT_FALSE(ComputeIsZstdFrame(b1, 4));
	EXPECT_FALSE(ComputeIsZstdFrame(b2, 4));
	EXPECT_FALSE(ComputeIsZstdFrame(b3, 4));
}

TEST(ZstdWireCompute, UsedZstdIsClearedByTheHuffmanFallback)
{
	// [KI] The case a live netgame caught: net_zstd_smart sent the packet as Huffman but the stats still counted it as zstd, so a fallback must report false.
	EXPECT_FALSE(ComputeUsedZstd(true, true));
}

TEST(ZstdWireCompute, UsedZstdHoldsWhenNoFallbackRan)
{
	// [KI] A packet zstd compressed and sent that way is the only case counted as zstd.
	EXPECT_TRUE(ComputeUsedZstd(true, false));
}

TEST(ZstdWireCompute, UsedZstdIsFalseWhenZstdNeverRan)
{
	// [KI] Both ways a packet that never reached the codec is counted as Huffman, whether or not the fallback flag is set.
	EXPECT_FALSE(ComputeUsedZstd(false, true));
	EXPECT_FALSE(ComputeUsedZstd(false, false));
}

TEST(ZstdWireCompute, FrameCheckIsNotExact)
{
	// [KI] A Huffman datagram whose first four bytes happen to be the magic is misread as zstd, about one packet in 2^32, which 3.3 accepts and this test documents rather than presenting the check as exact.
	const unsigned char huffmanThatLooksLikeZstd[] = { 0x28, 0xB5, 0x2F, 0xFD, 0x12, 0x34 };
	EXPECT_TRUE(ComputeIsZstdFrame(huffmanThatLooksLikeZstd, 6));
}

} // namespace
