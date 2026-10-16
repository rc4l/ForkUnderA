// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] SHA-256 and MD5 over a stream of bytes, with no library under them, so a build without OpenSSL (the browser's) still identifies its wads.

#ifndef ZX_DIGEST_COMPUTE_H
#define ZX_DIGEST_COMPUTE_H

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace zx {

struct Sha256State
{
	uint32_t h[8];
	uint64_t bytes;
	uint8_t block[64];
	size_t used;
};

void ComputeSha256Init(Sha256State &s);
void ComputeSha256Update(Sha256State &s, const void *data, size_t len);
void ComputeSha256Final(Sha256State &s, uint8_t out[32]);

struct Md5State
{
	uint32_t h[4];
	uint64_t bytes;
	uint8_t block[64];
	size_t used;
};

void ComputeMd5Init(Md5State &s);
void ComputeMd5Update(Md5State &s, const void *data, size_t len);
void ComputeMd5Final(Md5State &s, uint8_t out[16]);

// [rc4l] A digest as lowercase hex, as sha256sum and md5sum print it.
std::string ComputeDigestHex(const uint8_t *digest, size_t len);

} // namespace zx

#endif // ZX_DIGEST_COMPUTE_H
