// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/digest_compute.h"

#include <string.h>

namespace zx {

namespace {

const uint32_t kSha256K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint32_t Rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void Sha256Block(uint32_t h[8], const uint8_t *p)
{
	uint32_t w[64];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
	for (int i = 16; i < 64; i++)
	{
		const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
	for (int i = 0; i < 64; i++)
	{
		const uint32_t t1 = k + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
		const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
}

const uint32_t kMd5K[64] = {
	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
	0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
	0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
	0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
	0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
	0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
	0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
	0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

const int kMd5R[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

void Md5Block(uint32_t h[4], const uint8_t *p)
{
	uint32_t w[16];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
	uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
	for (int i = 0; i < 64; i++)
	{
		uint32_t f;
		int g;
		if (i < 16) { f = (b & c) | (~b & d); g = i; }
		else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
		else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
		else { f = c ^ (b | ~d); g = (7 * i) % 16; }
		const uint32_t t = d;
		d = c; c = b;
		b = b + Rotl(a + f + kMd5K[i] + w[g], kMd5R[i]);
		a = t;
	}
	h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}

// [rc4l] Both digests' shared Merkle-Damgard buffering: whole 64-byte blocks to `block`, the rest kept.
template <typename State>
void Absorb(State &s, const void *data, size_t len, void (*block)(uint32_t *, const uint8_t *))
{
	const uint8_t *p = (const uint8_t *)data;
	s.bytes += len;
	while (len > 0)
	{
		const size_t take = (64 - s.used) < len ? (64 - s.used) : len;
		memcpy(s.block + s.used, p, take);
		s.used += take; p += take; len -= take;
		if (s.used == 64)
		{
			block(s.h, s.block);
			s.used = 0;
		}
	}
}

// [rc4l] The 0x80 marker, zeros, and the message's bit length in the last eight bytes, big-endian for SHA-256 and little for MD5.
template <typename State>
void Pad(State &s, void (*block)(uint32_t *, const uint8_t *), bool bigEndian)
{
	const uint64_t bits = s.bytes * 8;
	const uint8_t marker = 0x80;
	Absorb(s, &marker, 1, block);
	const uint8_t zero = 0;
	while (s.used != 56) Absorb(s, &zero, 1, block);
	uint8_t len[8];
	for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (bigEndian ? 56 - i * 8 : i * 8));
	Absorb(s, len, 8, block);
}

} // namespace

void ComputeSha256Init(Sha256State &s)
{
	static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy(s.h, init, sizeof init);
	s.bytes = 0;
	s.used = 0;
}

void ComputeSha256Update(Sha256State &s, const void *data, size_t len)
{
	Absorb(s, data, len, Sha256Block);
}

void ComputeSha256Final(Sha256State &s, uint8_t out[32])
{
	Pad(s, Sha256Block, true);
	for (int i = 0; i < 32; i++) out[i] = (uint8_t)(s.h[i / 4] >> (24 - (i % 4) * 8));
}

void ComputeMd5Init(Md5State &s)
{
	static const uint32_t init[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
	memcpy(s.h, init, sizeof init);
	s.bytes = 0;
	s.used = 0;
}

void ComputeMd5Update(Md5State &s, const void *data, size_t len)
{
	Absorb(s, data, len, Md5Block);
}

void ComputeMd5Final(Md5State &s, uint8_t out[16])
{
	Pad(s, Md5Block, false);
	for (int i = 0; i < 16; i++) out[i] = (uint8_t)(s.h[i / 4] >> ((i % 4) * 8));
}

std::string ComputeDigestHex(const uint8_t *digest, size_t len)
{
	static const char kHex[] = "0123456789abcdef";
	std::string out;
	for (size_t i = 0; i < len; i++)
	{
		out += kHex[digest[i] >> 4];
		out += kHex[digest[i] & 0xF];
	}
	return out;
}

} // namespace zx
