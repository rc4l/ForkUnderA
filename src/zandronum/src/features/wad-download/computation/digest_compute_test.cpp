// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <algorithm>
#include <string.h>
#include <string>

#include "features/wad-download/computation/digest_compute.h"

using namespace zx;

namespace {

std::string Sha256(const std::string &text, size_t chunk = 0)
{
	Sha256State s;
	ComputeSha256Init(s);
	if (chunk == 0) ComputeSha256Update(s, text.data(), text.size());
	else for (size_t i = 0; i < text.size(); i += chunk) ComputeSha256Update(s, text.data() + i, std::min(chunk, text.size() - i));
	uint8_t out[32];
	ComputeSha256Final(s, out);
	return ComputeDigestHex(out, 32);
}

std::string Md5(const std::string &text, size_t chunk = 0)
{
	Md5State s;
	ComputeMd5Init(s);
	if (chunk == 0) ComputeMd5Update(s, text.data(), text.size());
	else for (size_t i = 0; i < text.size(); i += chunk) ComputeMd5Update(s, text.data() + i, std::min(chunk, text.size() - i));
	uint8_t out[16];
	ComputeMd5Final(s, out);
	return ComputeDigestHex(out, 16);
}

} // namespace

// [rc4l] FIPS 180-2's vectors, including the two-block one and a message filling a block exactly.
TEST(Digest, Sha256MatchesTheStandardVectors)
{
	EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", Sha256(""));
	EXPECT_EQ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", Sha256("abc"));
	EXPECT_EQ("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
		Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
	EXPECT_EQ("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", Sha256(std::string(1000000, 'a'), 4096));
	EXPECT_EQ(Sha256(std::string(64, 'x')), Sha256(std::string(64, 'x'), 7));
}

// [rc4l] RFC 1321's vectors, fed whole and in odd pieces.
TEST(Digest, Md5MatchesTheStandardVectors)
{
	EXPECT_EQ("d41d8cd98f00b204e9800998ecf8427e", Md5(""));
	EXPECT_EQ("900150983cd24fb0d6963f7d28e17f72", Md5("abc"));
	EXPECT_EQ("f96b697d7cb7938d525a2f31aaf161d0", Md5("message digest"));
	EXPECT_EQ("57edf4a22be3c955ac49da2e2107b67a", Md5("12345678901234567890123456789012345678901234567890123456789012345678901234567890", 13));
	EXPECT_EQ("7707d6ae4e027c70eea2a935c2296f21", Md5(std::string(1000000, 'a'), 4096));
}

TEST(Digest, HexIsLowercase)
{
	const uint8_t bytes[] = { 0x00, 0xAB, 0xF0 };
	EXPECT_EQ("00abf0", ComputeDigestHex(bytes, 3));
}
