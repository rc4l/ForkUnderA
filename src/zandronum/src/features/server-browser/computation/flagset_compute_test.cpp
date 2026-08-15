// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/server-browser/computation/flagset_compute.h"

using namespace zx;

namespace
{

std::vector<FlagBit> ThreeBits()
{
	std::vector<FlagBit> bits;
	bits.push_back(FlagBit("sv_a", 1u << 0));
	bits.push_back(FlagBit("sv_b", 1u << 1));
	bits.push_back(FlagBit("sv_c", 1u << 4));
	return bits;
}

} // namespace

// ---------------------------------------------------------------- single bits

TEST(FlagSet, ReadsAndWritesOneBit)
{
	EXPECT_FALSE(FlagIsOn(0, 1u << 3));
	EXPECT_TRUE(FlagIsOn(1u << 3, 1u << 3));

	EXPECT_EQ(1u << 3, FlagSet(0, 1u << 3, true));
	EXPECT_EQ(0u, FlagSet(1u << 3, 1u << 3, false));
}

TEST(FlagSet, LeavesEveryOtherBitAlone)
{
	// This is what keeps a pasted number's unknown bits alive across an edit.
	const unsigned int start = 0xF0F0F0F0u;

	EXPECT_EQ(start | 1u, FlagSet(start, 1u, true));
	EXPECT_EQ(start & ~0x10u, FlagSet(start, 0x10u, false));
}

TEST(FlagSet, ABitOfZeroIsNoBitAtAll)
{
	// Guards the case where a name has no bit behind it: doing nothing beats clearing the field.
	EXPECT_EQ(1234u, FlagSet(1234u, 0, true));
	EXPECT_EQ(1234u, FlagSet(1234u, 0, false));
	EXPECT_FALSE(FlagIsOn(0xFFFFFFFFu, 0));
}

TEST(FlagSet, SettingTwiceIsSettingOnce)
{
	const unsigned int once = FlagSet(0, 4u, true);
	EXPECT_EQ(once, FlagSet(once, 4u, true));
}

// ---------------------------------------------------------------- unknown bits

TEST(UnknownBits, AreTheOnesNoNameAccountsFor)
{
	const std::vector<FlagBit> bits = ThreeBits();

	EXPECT_EQ(0x13u, KnownMask(bits));
	EXPECT_EQ(0u, UnknownBits(0x13u, bits));
	EXPECT_EQ(0x20u, UnknownBits(0x33u, bits));
}

TEST(UnknownBits, SurviveAnEditOfAKnownOne)
{
	// The whole point: paste a number from a newer build, toggle something, and the bit this build
	// cannot name is still there.
	const std::vector<FlagBit> bits = ThreeBits();
	const unsigned int pasted = 0x80000001u;		// sv_a, plus a bit we have no name for

	ASSERT_EQ(0x80000000u, UnknownBits(pasted, bits));

	const unsigned int edited = FlagSet(pasted, 1u << 1, true);
	EXPECT_EQ(0x80000000u, UnknownBits(edited, bits)) << "the unnamed bit must not be lost";
	EXPECT_TRUE(FlagIsOn(edited, 1u << 1));
}

TEST(CountBits, CountsThem)
{
	EXPECT_EQ(0, CountBits(0));
	EXPECT_EQ(1, CountBits(1u << 31));
	EXPECT_EQ(32, CountBits(0xFFFFFFFFu));
	EXPECT_EQ(4, CountBits(0xF0u));
}

// ---------------------------------------------------------------- the number box

TEST(ParseFlagNumber, TakesAPlainNumber)
{
	unsigned int v = 99;
	EXPECT_TRUE(ParseFlagNumber("9584640", v));
	EXPECT_EQ(9584640u, v);
}

TEST(ParseFlagNumber, ForgivesSpaceAroundIt)
{
	// Pasting from a console line brings it along.
	unsigned int v = 0;
	EXPECT_TRUE(ParseFlagNumber("  1024\t", v));
	EXPECT_EQ(1024u, v);
}

TEST(ParseFlagNumber, AnEmptyBoxIsZero)
{
	unsigned int v = 7;
	EXPECT_TRUE(ParseFlagNumber("", v));
	EXPECT_EQ(0u, v);

	EXPECT_TRUE(ParseFlagNumber("   ", v));
	EXPECT_EQ(0u, v);
}

TEST(ParseFlagNumber, RefusesRatherThanReadingHalfOfIt)
{
	// "123abc" is not 123, it is a mistake -- and taking half of it is how somebody hosts settings
	// they never chose.
	unsigned int v = 0;
	EXPECT_FALSE(ParseFlagNumber("123abc", v));
	EXPECT_FALSE(ParseFlagNumber("0x40", v));
	EXPECT_FALSE(ParseFlagNumber("-8", v));
	EXPECT_FALSE(ParseFlagNumber("1 2", v));
}

TEST(ParseFlagNumber, RefusesWhatWillNotFitRatherThanWrapping)
{
	unsigned int v = 0;
	EXPECT_TRUE(ParseFlagNumber("4294967295", v));
	EXPECT_EQ(0xFFFFFFFFu, v);

	EXPECT_FALSE(ParseFlagNumber("4294967296", v));
	EXPECT_FALSE(ParseFlagNumber("99999999999999999999", v));
}

TEST(FormatFlagNumber, RoundTripsWithTheParser)
{
	// The property that matters: what the box shows must read back as what it shows.
	const unsigned int values[] = { 0u, 1u, 9584640u, 89888610u, 0x80000000u, 0xFFFFFFFFu };

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
	{
		unsigned int back = 0;
		ASSERT_TRUE(ParseFlagNumber(FormatFlagNumber(values[i]), back)) << values[i];
		EXPECT_EQ(values[i], back);
	}
}

TEST(FormatFlagNumber, IsUnsignedRatherThanSigned)
{
	// The top bit is a flag like any other, not a minus sign.
	EXPECT_EQ("2147483648", FormatFlagNumber(0x80000000u));
	EXPECT_EQ("4294967295", FormatFlagNumber(0xFFFFFFFFu));
}

// ---------------------------------------------------------------- switches and number agree

TEST(FlagSet, TheSwitchesRebuildTheNumberTheyCameFrom)
{
	// Decode to switches, encode back, and nothing may move.
	const std::vector<FlagBit> bits = ThreeBits();
	const unsigned int start = 0x13u;

	unsigned int rebuilt = UnknownBits(start, bits);
	for (size_t i = 0; i < bits.size(); ++i)
		rebuilt = FlagSet(rebuilt, bits[i].bit, FlagIsOn(start, bits[i].bit));

	EXPECT_EQ(start, rebuilt);
}

TEST(FlagSet, TheRebuildKeepsUnknownBitsToo)
{
	const std::vector<FlagBit> bits = ThreeBits();
	const unsigned int start = 0xDEADBEEFu;

	unsigned int rebuilt = UnknownBits(start, bits);
	for (size_t i = 0; i < bits.size(); ++i)
		rebuilt = FlagSet(rebuilt, bits[i].bit, FlagIsOn(start, bits[i].bit));

	EXPECT_EQ(start, rebuilt);
}

// ---------------------------------------------------------------- field order

TEST(IsFlagFieldName, TellsAFieldFromASwitchInsideOne)
{
	// A saved preset's settings are dmflags alongside sv_maxlives, and "the flags" means the first
	// kind only.
	EXPECT_TRUE(IsFlagFieldName("dmflags"));
	EXPECT_TRUE(IsFlagFieldName("zacompatflags"));
	EXPECT_TRUE(IsFlagFieldName("lmsallowedweapons"));

	// Not in the preferred order list, and still a field: the walk appends it.
	EXPECT_TRUE(IsFlagFieldName("sv_forbidvoteflags"));

	EXPECT_FALSE(IsFlagFieldName("sv_nomonsters"));
	EXPECT_FALSE(IsFlagFieldName("sv_maxlives"));
	EXPECT_FALSE(IsFlagFieldName(""));
}

TEST(FlagFieldOrder, IsTheOrderTheyAreAlwaysQuotedIn)
{
	std::vector<std::string> found;
	found.push_back("zacompatflags");
	found.push_back("dmflags2");
	found.push_back("dmflags");
	found.push_back("compatflags");

	const std::vector<std::string> out = FlagFieldOrder(found);

	ASSERT_EQ(4u, out.size());
	EXPECT_EQ("dmflags", out[0]);
	EXPECT_EQ("dmflags2", out[1]);
	EXPECT_EQ("compatflags", out[2]);
	EXPECT_EQ("zacompatflags", out[3]);
}

TEST(FlagFieldOrder, KeepsAFieldItHasNeverHeardOf)
{
	// A build with a field this list does not name still has to show it: hiding it would be the
	// screen lying about what it edits.
	std::vector<std::string> found;
	found.push_back("somethingnew");
	found.push_back("dmflags");

	const std::vector<std::string> out = FlagFieldOrder(found);

	ASSERT_EQ(2u, out.size());
	EXPECT_EQ("dmflags", out[0]);
	EXPECT_EQ("somethingnew", out[1]) << "unknown fields go last, never missing";
}

TEST(FlagFieldOrder, NamesNothingTheEngineDidNotReport)
{
	std::vector<std::string> found;
	found.push_back("dmflags");

	const std::vector<std::string> out = FlagFieldOrder(found);

	ASSERT_EQ(1u, out.size());
	EXPECT_EQ("dmflags", out[0]);
}

TEST(FlagFieldOrder, AnEmptyEngineIsAnEmptyList)
{
	EXPECT_TRUE(FlagFieldOrder(std::vector<std::string>()).empty());
}

// ---------------------------------------------------------------- the empty shapes

// [rc4l] The default constructors are what let these sit in a vector before they are filled in, and
// the values they start at are load-bearing: a field read as zero is a field with nothing set, which
// is what the editor shows for a cvar nobody has touched.
TEST(FlagShapes, StartAtNothingRatherThanAtWhateverWasOnTheStack)
{
	const FlagBit bit;
	EXPECT_TRUE(bit.name.empty());
	EXPECT_EQ(0u, bit.bit);

	const FlagField field;
	EXPECT_TRUE(field.name.empty());
	EXPECT_TRUE(field.bits.empty());
	EXPECT_EQ(0u, field.value);
}
