// [rc4l] Tests for the MBF21 thing-flag mnemonic lookup. Pins every spec mnemonic to its bit,
// verifies case-insensitivity, and locks the ALLFLAGS union so a mistyped/duplicated table entry
// can't slip through.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "features/mbf21/computation/mbf21_flags_compute.h"

using namespace zx::mbf21;

// Every mnemonic the spec defines, in field order.
static const struct { const char *name; unsigned bit; } kExpected[] =
{
	{ "LOGRAV",         DEH21F_LOGRAV },
	{ "SHORTMRANGE",    DEH21F_SHORTMRANGE },
	{ "DMGIGNORED",     DEH21F_DMGIGNORED },
	{ "NORADIUSDMG",    DEH21F_NORADIUSDMG },
	{ "FORCERADIUSDMG", DEH21F_FORCERADIUSDMG },
	{ "HIGHERMPROB",    DEH21F_HIGHERMPROB },
	{ "RANGEHALF",      DEH21F_RANGEHALF },
	{ "NOTHRESHOLD",    DEH21F_NOTHRESHOLD },
	{ "LONGMELEE",      DEH21F_LONGMELEE },
	{ "BOSS",           DEH21F_BOSS },
	{ "MAP07BOSS1",     DEH21F_MAP07BOSS1 },
	{ "MAP07BOSS2",     DEH21F_MAP07BOSS2 },
	{ "E1M8BOSS",       DEH21F_E1M8BOSS },
	{ "E2M8BOSS",       DEH21F_E2M8BOSS },
	{ "E3M8BOSS",       DEH21F_E3M8BOSS },
	{ "E4M6BOSS",       DEH21F_E4M6BOSS },
	{ "E4M8BOSS",       DEH21F_E4M8BOSS },
	{ "RIP",            DEH21F_RIP },
	{ "FULLVOLSOUNDS",  DEH21F_FULLVOLSOUNDS },
};

TEST(Mbf21Flags, EveryMnemonicResolvesToItsBit)
{
	for (const auto &e : kExpected)
	{
		EXPECT_EQ(ComputeMbf21ThingBitFromName(e.name), e.bit) << e.name;
	}
}

TEST(Mbf21Flags, LookupIsCaseInsensitive)
{
	EXPECT_EQ(ComputeMbf21ThingBitFromName("rip"), DEH21F_RIP);
	EXPECT_EQ(ComputeMbf21ThingBitFromName("Map07Boss1"), DEH21F_MAP07BOSS1);
	EXPECT_EQ(ComputeMbf21ThingBitFromName("fUlLvOlSoUnDs"), DEH21F_FULLVOLSOUNDS);
}

TEST(Mbf21Flags, UnknownAndNullReturnZero)
{
	EXPECT_EQ(ComputeMbf21ThingBitFromName("NOTAFLAG"), 0u);
	EXPECT_EQ(ComputeMbf21ThingBitFromName(""), 0u);
	EXPECT_EQ(ComputeMbf21ThingBitFromName(nullptr), 0u);
	// A prefix of a real name must not match (length matters).
	EXPECT_EQ(ComputeMbf21ThingBitFromName("RI"), 0u);
	EXPECT_EQ(ComputeMbf21ThingBitFromName("RIPPER"), 0u);
	// Chars just above 'z' (0x7B..) are not folded and match nothing -- exercises the upper bound
	// of the lowercase range check.
	EXPECT_EQ(ComputeMbf21ThingBitFromName("{"), 0u);
	EXPECT_EQ(ComputeMbf21ThingBitFromName("rip}"), 0u);
}

TEST(Mbf21Flags, EachBitIsDistinctAndAllflagsIsTheirUnion)
{
	unsigned unionBits = 0;
	for (const auto &e : kExpected)
	{
		// No two mnemonics share a bit.
		EXPECT_EQ(unionBits & e.bit, 0u) << "duplicate bit for " << e.name;
		unionBits |= e.bit;
	}
	EXPECT_EQ(unionBits, (unsigned)DEH21F_ALLFLAGS);
}

// ---- weapon flags ----------------------------------------------------------

static const struct { const char *name; unsigned bit; } kWeaponExpected[] =
{
	{ "NOTHRUST",       DEH21WF_NOTHRUST },
	{ "SILENT",         DEH21WF_SILENT },
	{ "NOAUTOFIRE",     DEH21WF_NOAUTOFIRE },
	{ "FLEEMELEE",      DEH21WF_FLEEMELEE },
	{ "AUTOSWITCHFROM", DEH21WF_AUTOSWITCHFROM },
	{ "NOAUTOSWITCHTO", DEH21WF_NOAUTOSWITCHTO },
};

TEST(Mbf21Flags, EveryWeaponMnemonicResolvesToItsBit)
{
	for (const auto &e : kWeaponExpected)
		EXPECT_EQ(ComputeMbf21WeaponBitFromName(e.name), e.bit) << e.name;
}

TEST(Mbf21Flags, WeaponLookupIsCaseInsensitiveAndRejectsUnknown)
{
	EXPECT_EQ(ComputeMbf21WeaponBitFromName("silent"), DEH21WF_SILENT);
	EXPECT_EQ(ComputeMbf21WeaponBitFromName("NoAutoSwitchTo"), DEH21WF_NOAUTOSWITCHTO);
	EXPECT_EQ(ComputeMbf21WeaponBitFromName("NOTAFLAG"), 0u);
	EXPECT_EQ(ComputeMbf21WeaponBitFromName(nullptr), 0u);
}

TEST(Mbf21Flags, WeaponBitsAreDistinctAndUnionIsAllflags)
{
	unsigned unionBits = 0;
	for (const auto &e : kWeaponExpected)
	{
		EXPECT_EQ(unionBits & e.bit, 0u) << "duplicate bit for " << e.name;
		unionBits |= e.bit;
	}
	EXPECT_EQ(unionBits, (unsigned)DEH21WF_ALLFLAGS);
}
