// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kaori Ishida
//
// [KI] The pure decisions of Zandronum 3.3's zstd packet compression — whether a packet tries zstd, whether it falls back to Huffman, and whether received bytes are a zstd frame — decided with no codec and no socket.

#ifndef ZX_ZSTDWIRE_COMPUTE_H
#define ZX_ZSTDWIRE_COMPUTE_H

namespace zx { namespace nettransport {

// [KI] net_zstd_level clamped to the valid range 1 to maxLevel, mirroring 3.3's CUSTOM_CVAR clamp.
int ComputeZstdLevelClamped(int level, int maxLevel);

// [KI] Whether an outgoing packet is compressed with zstd: only when the caller asked and the payload reaches net_zstd_threshold bytes.
bool ComputeShouldTryZstd(bool useZstdRequested, unsigned int packetSize, int thresholdBytes);

// [KI] Whether the packet goes as Huffman after the zstd stage: always when zstd did not run, and under net_zstd_smart also when the zstd form is no smaller than the input.
bool ComputeHuffmanFallback(bool zstdCompressed, bool smartEnabled, int compressedSize, unsigned int originalSize);

// [KI] Whether the packet that actually went out was zstd-compressed, which the Huffman fallback undoes, so a fallback packet is counted as Huffman and not as zstd by the net traffic stats.
bool ComputeUsedZstd(bool zstdCompressed, bool fellBackToHuffman);

// [KI] Whether a received datagram is a zstd frame by its first four bytes, 28 B5 2F FD on the wire; a Huffman datagram that happens to start with them is misread, about one in 2^32, which 3.3 accepts.
bool ComputeIsZstdFrame(const unsigned char *bytes, int length);

}} // namespace zx::nettransport

#endif // ZX_ZSTDWIRE_COMPUTE_H
