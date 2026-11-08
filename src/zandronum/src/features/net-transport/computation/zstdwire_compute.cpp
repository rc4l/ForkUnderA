// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] The pure decisions of Zandronum 3.3's zstd packet compression, extracted from network.cpp's encode and decode glue so they are testable without the engine or libzstd.

#include "zstdwire_compute.h"

namespace zx { namespace nettransport {

//*****************************************************************************
//
int ComputeZstdLevelClamped(int level, int maxLevel)
{
	if (level < 1)
		return 1;
	if (level > maxLevel)
		return maxLevel;
	return level;
}

//*****************************************************************************
//
bool ComputeShouldTryZstd(bool useZstdRequested, unsigned int packetSize, int thresholdBytes)
{
	return useZstdRequested && static_cast<int>(packetSize) >= thresholdBytes;
}

//*****************************************************************************
//
bool ComputeHuffmanFallback(bool zstdCompressed, bool smartEnabled, int compressedSize, unsigned int originalSize)
{
	return !zstdCompressed || (smartEnabled && compressedSize >= static_cast<int>(originalSize));
}

//*****************************************************************************
//
// [KI] Ported from zandronum@66309c7282223dd15e8764448a3e4f2c3e784c0f: the fallback clears 3.3's didUseZstd, so a packet re-encoded as Huffman is no longer counted as a zstd one.
bool ComputeUsedZstd(bool zstdCompressed, bool fellBackToHuffman)
{
	return zstdCompressed && !fellBackToHuffman;
}

//*****************************************************************************
//
bool ComputeIsZstdFrame(const unsigned char *bytes, int length)
{
	// [KI] ZSTD_MAGICNUMBER 0xFD2FB528 as the wire lays it out little-endian, so the check reads the frame and not the host's byte order.
	return length >= 4 && bytes[0] == 0x28 && bytes[1] == 0xB5 && bytes[2] == 0x2F && bytes[3] == 0xFD;
}

}} // namespace zx::nettransport
