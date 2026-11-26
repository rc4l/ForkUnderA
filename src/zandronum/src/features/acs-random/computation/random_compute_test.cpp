#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "features/acs-random/computation/random_compute.h"

using namespace zx::acsrandom;

// [Doruk-Aksoy] A stream's key carries the prefix, its own namespace for CLIENTSIDE scripts, and its name in lower case, so "Loot" and "loot" are one.
TEST(AcsRandom, StreamKeys)
{
	EXPECT_EQ(ComputeStreamKey("Loot", false), "fua.acs.play:loot");
	EXPECT_EQ(ComputeStreamKey("loot", false), "fua.acs.play:loot");
	EXPECT_EQ(ComputeStreamKey("Loot", true), "fua.acs.client:loot");
	EXPECT_EQ(ComputeStreamKey("ACS", false), "fua.acs.play:acs");
	EXPECT_EQ(ComputeStreamKey("", false), "");
	EXPECT_EQ(ComputeStreamKey(nullptr, false), "");
	// [Doruk-Aksoy] No gameplay name lands on a clientside stream's key.
	EXPECT_NE(ComputeStreamKey("client:x", false), ComputeStreamKey("x", true));
	EXPECT_EQ(ComputeStreamKey(std::string(kMaxName, 'a').c_str(), false).size(), std::string(kPrefix).size() + kMaxName);
	EXPECT_EQ(ComputeStreamKey(std::string(kMaxName + 1, 'a').c_str(), false), "");
}

// [Doruk-Aksoy] Whole numbers cover both ends, either way round, and the widest range without overflow.
TEST(AcsRandom, WholeRange)
{
	EXPECT_EQ(ComputeRandomRange(1, 6, 0), 1);
	EXPECT_EQ(ComputeRandomRange(1, 6, 5), 6);
	EXPECT_EQ(ComputeRandomRange(1, 6, 6), 1);
	EXPECT_EQ(ComputeRandomRange(6, 1, 5), 6);
	EXPECT_EQ(ComputeRandomRange(-3, -3, 12345), -3);
	EXPECT_EQ(ComputeRandomRange(INT32_MIN, INT32_MAX, 0), INT32_MIN);
	EXPECT_EQ(ComputeRandomRange(INT32_MIN, INT32_MAX, 0xFFFFFFFFu), INT32_MAX);
}

// [Doruk-Aksoy] Fixed point covers both ends, the middle in between, either way round.
TEST(AcsRandom, FixedRange)
{
	const int32_t one = 65536;
	EXPECT_EQ(ComputeFixedRange(0, one, 0), 0);
	EXPECT_EQ(ComputeFixedRange(0, one, 0xFFFFFFFFu), one);
	EXPECT_EQ(ComputeFixedRange(0, one, 0x80000000u), one / 2);
	EXPECT_EQ(ComputeFixedRange(one, 0, 0), 0);
	EXPECT_EQ(ComputeFixedRange(-one, one, 0xFFFFFFFFu), one);
	EXPECT_EQ(ComputeFixedRange(INT32_MIN, INT32_MAX, 0xFFFFFFFFu), INT32_MAX);
}

// [Doruk-Aksoy] The savegame chunk round-trips its keys and refuses a cut or foreign one.
TEST(AcsRandom, KeysChunk)
{
	const std::vector<std::string> keys = { "fua.acs.play:loot", "fua.acs.client:sparks" };
	const std::vector<uint8_t> chunk = ComputeEncodeKeys(keys);
	std::vector<std::string> back;
	ASSERT_TRUE(ComputeDecodeKeys(chunk.data(), chunk.size(), back));
	EXPECT_EQ(back, keys);
	EXPECT_FALSE(ComputeDecodeKeys(chunk.data(), chunk.size() - 1, back));
	EXPECT_FALSE(ComputeDecodeKeys(chunk.data(), 3, back));
	EXPECT_FALSE(ComputeDecodeKeys(nullptr, 0, back));
	std::vector<uint8_t> longer = chunk;
	longer.push_back(0);
	EXPECT_FALSE(ComputeDecodeKeys(longer.data(), longer.size(), back));
	const std::vector<uint8_t> foreign = ComputeEncodeKeys({ "ACS" });
	EXPECT_FALSE(ComputeDecodeKeys(foreign.data(), foreign.size(), back));
	std::vector<uint8_t> truncatedKey = { 1, 0, 0, 0, 5, 'f', 'u' };
	std::vector<uint8_t> noKey = { 1, 0, 0, 0 };
	EXPECT_FALSE(ComputeDecodeKeys(noKey.data(), noKey.size(), back));
	EXPECT_FALSE(ComputeDecodeKeys(truncatedKey.data(), truncatedKey.size(), back));
	const std::vector<uint8_t> none = ComputeEncodeKeys({});
	EXPECT_TRUE(ComputeDecodeKeys(none.data(), none.size(), back));
	EXPECT_TRUE(back.empty());
}

// [Doruk-Aksoy] Only keys under the prefixes, with a name of a usable length, are ACS streams.
TEST(AcsRandom, KeysThatAreOurs)
{
	EXPECT_TRUE(ComputeKeyIsOurs("fua.acs.play:loot"));
	EXPECT_TRUE(ComputeKeyIsOurs("fua.acs.client:loot"));
	EXPECT_FALSE(ComputeKeyIsOurs("fua.acs.play:"));
	EXPECT_FALSE(ComputeKeyIsOurs("fua.acs.client:"));
	EXPECT_FALSE(ComputeKeyIsOurs("fua.acs.loot"));
	EXPECT_FALSE(ComputeKeyIsOurs("ACS"));
	EXPECT_FALSE(ComputeKeyIsOurs(std::string(kPrefix) + std::string(kMaxName + 1, 'a')));
}
