// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] NETWORK_DecodeIntoMessage, the decode every received datagram goes through: a packet of ours comes back whole, and a crafted one never writes past the message, which the raw buffer's size as the limit once let it do.

#include "doomtype.h"
#include "networkshared.h"
#include "huffman/huffman.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

// [rc4l] The receive buffer as NETWORK_Construct sizes it: room for MAX_UDP_PACKET decoded at the shortest code's eight thirds.
const ULONG kMessageSize = (MAX_UDP_PACKET * 8) / 3 + 1;

void Huffman()
{
	static bool built = false;
	if (!built) HUFFMAN_Construct();
	built = true;
}

std::vector<unsigned char> Encode(const std::vector<unsigned char> &plain)
{
	std::vector<unsigned char> raw(plain.size() * 2 + 16);
	int size = (int)raw.size();
	HUFFMAN_Encode(plain.data(), raw.data(), (int)plain.size(), &size);
	raw.resize(size > 0 ? size : 0);
	return raw;
}

// [rc4l] The byte whose code is shortest, which a crafted datagram repeats to decode to the most.
unsigned char ShortestCoded()
{
	size_t best = (size_t)-1;
	unsigned char which = 0;
	for (int b = 0; b < 256; b++)
	{
		const size_t size = Encode(std::vector<unsigned char>(4096, (unsigned char)b)).size();
		if (size < best) { best = size; which = (unsigned char)b; }
	}
	return which;
}

} // namespace

TEST(NetworkDecodeIntoMessage, APacketOfOursComesBackWhole)
{
	Huffman();
	std::vector<unsigned char> plain;
	for (int i = 0; i < 700; i++) plain.push_back((unsigned char)(i * 7 + 3));
	const std::vector<unsigned char> raw = Encode(plain);
	NETBUFFER_s message;
	message.Init(kMessageSize, BUFFERTYPE_READ);
	ASSERT_EQ(plain.size(), NETWORK_DecodeIntoMessage(raw.data(), (int)raw.size(), message));
	EXPECT_EQ(plain, std::vector<unsigned char>(message.pbData, message.pbData + message.ulCurrentSize));
	EXPECT_EQ(message.pbData, message.ByteStream.pbStream);
	EXPECT_EQ(message.pbData + plain.size(), message.ByteStream.pbStreamEnd);
	EXPECT_EQ(-1, message.ByteStream.bitShift);
	message.Free();
}

TEST(NetworkDecodeIntoMessage, ACraftedDatagramNeverWritesPastTheMessage)
{
	Huffman();
	// [rc4l] Half again the message's size in the shortest code: small enough for the receive path's size check, decoding to more than the message holds.
	const std::vector<unsigned char> raw = Encode(std::vector<unsigned char>(kMessageSize * 3 / 2, ShortestCoded()));
	ASSERT_LT(raw.size(), (size_t)kMessageSize);
	ASSERT_NE(0xff, raw[0]);
	// [rc4l] The message's bytes followed by guard bytes, so a write past it shows without a sanitizer.
	std::vector<unsigned char> storage(kMessageSize + 4096, 0x5a);
	NETBUFFER_s message;
	message.pbData = storage.data();
	message.ulMaxSize = kMessageSize;
	message.BufferType = BUFFERTYPE_READ;
	EXPECT_EQ(0u, NETWORK_DecodeIntoMessage(raw.data(), (int)raw.size(), message));
	EXPECT_EQ(0u, message.ulCurrentSize);
	EXPECT_EQ(message.ByteStream.pbStream, message.ByteStream.pbStreamEnd);
	for (size_t i = kMessageSize; i < storage.size(); i++) ASSERT_EQ(0x5a, storage[i]) << "written past the message at " << i;
	message.pbData = NULL;
}

TEST(NetworkDecodeIntoMessage, AnUnencodedPacketIsCopiedAndOneTooBigRefused)
{
	Huffman();
	const std::vector<unsigned char> raw = { 0xff, 1, 2, 3, 4, 5 };
	NETBUFFER_s message;
	message.Init(8, BUFFERTYPE_READ);
	ASSERT_EQ(5u, NETWORK_DecodeIntoMessage(raw.data(), (int)raw.size(), message));
	EXPECT_EQ(std::vector<unsigned char>({ 1, 2, 3, 4, 5 }), std::vector<unsigned char>(message.pbData, message.pbData + 5));
	std::vector<unsigned char> big(20, 9);
	big[0] = 0xff;
	EXPECT_EQ(0u, NETWORK_DecodeIntoMessage(big.data(), (int)big.size(), message));
	message.Free();
}
