// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The ini round trip, tested on the real FConfigFile rather than a model of it.
//
// Written after a value came back from the ini shorter than it went in, and every unit test around
// it passed: the pure functions were right and the seam under them was not. ReadLine is fgets, so a
// line longer than its 256-byte buffer arrived as two, the head keeping a cut-off value and the tail
// being dropped for having no '='. Nothing reported an error, and the corruption only showed up as a
// half a URL in a mirror list.

#include "gtest/gtest.h"

#include "configfile.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

// Somewhere writable on every platform we build for: P_tmpdir is POSIX and MSVC has no such macro.
std::string TempPath(const char *name)
{
	const char *dir = std::getenv("TMPDIR");
	if (dir == NULL) dir = std::getenv("TEMP");
	if (dir == NULL) dir = std::getenv("TMP");
	if (dir == NULL) dir = ".";
	return std::string(dir) + "/" + name;
}

// [MobiusU9B] Gives config the one setting every test here writes, and points it at path.
void SetSomeCvar(FConfigFile &config, const std::string &path, const std::string &value)
{
	config.SetSection("GlobalSettings", true);
	config.SetValueForKey("some_cvar", value.c_str());
	config.ChangePathName(path.c_str());
}

// [MobiusU9B] The value some_cvar holds in the ini at path, or a marker saying why there is none.
std::string ReadSomeCvar(const std::string &path)
{
	FConfigFile in(path.c_str());
	if (!in.SetSection("GlobalSettings"))
		return "<no section>";

	const char *got = in.GetValueForKey("some_cvar");
	return got != NULL ? got : "<no key>";
}

// A config written to a temp file and read back, which is the trip a setting actually makes.
std::string RoundTrip(const std::string &value)
{
	char name[64];
	std::snprintf(name, sizeof name, "zx_cfgtest_%d.ini", (int)value.size());
	const std::string path = TempPath(name);

	{
		FConfigFile out;
		SetSomeCvar(out, path, value);
		out.WriteConfigFile();
	}

	const std::string got = ReadSomeCvar(path);
	std::remove(path.c_str());
	return got;
}

// Seven WAD mirrors: the value whose length found this, at 250 bytes plus a 21-byte key.
const char *const kMirrorList =
	"https://static.allfearthesentinel.com/wads/ https://euroboros.net/zandronum/wads/ "
	"https://static.audrealms.org/wads/ http://grandpachuck.org/files/wads/ "
	"https://wads.doomleague.org/ https://wads.firestick.games/ "
	"https://static.action.fapnow.xyz/wads/";

} // namespace

TEST(ConfigFileRoundTrip, KeepsAValueShorterThanTheReadBuffer)
{
	const std::string value(100, 'x');

	EXPECT_EQ(value, RoundTrip(value));
}

TEST(ConfigFileRoundTrip, KeepsAValueLongerThanTheReadBuffer)
{
	// The regression: 255 bytes of line survived and everything past it was lost.
	const std::string value(400, 'x');

	EXPECT_EQ(value, RoundTrip(value));
}

TEST(ConfigFileRoundTrip, KeepsTheWholeMirrorListTheEngineShips)
{
	// The exact value that broke, so shipping an eighth mirror cannot quietly cut the seventh.
	EXPECT_EQ(std::string(kMirrorList), RoundTrip(kMirrorList));
}

TEST(ConfigFileRoundTrip, KeepsAValueOnEitherSideOfTheBufferBoundary)
{
	// The key is 9 bytes plus '=', so these straddle the 256-byte line buffer exactly.
	for (size_t len = 240; len <= 260; ++len)
	{
		const std::string value(len, 'y');

		EXPECT_EQ(value, RoundTrip(value)) << "length " << len;
	}
}

TEST(ConfigFileRoundTrip, DoesNotTurnALongValueIntoASecondKey)
{
	// How the truncation showed itself in the wild: the tail of the line came back as its own entry.
	const std::string value(400, 'z');
	const std::string path = TempPath("zx_cfgtest_tail.ini");

	{
		FConfigFile out;
		SetSomeCvar(out, path, value);
		out.WriteConfigFile();
	}

	FConfigFile in(path.c_str());
	std::remove(path.c_str());
	ASSERT_TRUE(in.SetSection("GlobalSettings"));

	int entries = 0;
	const char *key;
	const char *val;
	while (in.NextInSection(key, val))
		++entries;

	EXPECT_EQ(1, entries);
}

TEST(ConfigFileWriteTo, WritesTheCopyWhereAsked)
{
	const std::string original = TempPath("zx_cfgtest_writeto_original.ini");
	const std::string copy = TempPath("zx_cfgtest_writeto_copy.ini");
	std::remove(original.c_str());

	FConfigFile config;
	SetSomeCvar(config, original, "saved");
	ASSERT_TRUE(config.WriteConfigFileTo(copy.c_str()));

	EXPECT_EQ("saved", ReadSomeCvar(copy));
	EXPECT_EQ("<no section>", ReadSomeCvar(original)) << "writing a copy must not create the original";
	std::remove(copy.c_str());
}

TEST(ConfigFileWriteTo, LeavesLaterSavesOnTheOriginalPath)
{
	// [MobiusU9B] The regression: after `writeini other.ini`, the config's own path pointed at the copy, so every later save went there.
	const std::string original = TempPath("zx_cfgtest_keep_original.ini");
	const std::string copy = TempPath("zx_cfgtest_keep_copy.ini");

	FConfigFile config;
	SetSomeCvar(config, original, "first");
	ASSERT_TRUE(config.WriteConfigFileTo(copy.c_str()));
	EXPECT_EQ(original, config.GetPathName());

	config.SetValueForKey("some_cvar", "second");
	ASSERT_TRUE(config.WriteConfigFile());

	EXPECT_EQ("second", ReadSomeCvar(original));
	EXPECT_EQ("first", ReadSomeCvar(copy));
	std::remove(original.c_str());
	std::remove(copy.c_str());
}

TEST(ConfigFileWriteTo, ReportsAPathItCannotWrite)
{
	const std::string original = TempPath("zx_cfgtest_unwritable_original.ini");

	FConfigFile config;
	SetSomeCvar(config, original, "value");

	EXPECT_FALSE(config.WriteConfigFileTo(TempPath("zx_no_such_dir/zx_cfgtest.ini").c_str()));
	EXPECT_EQ(original, config.GetPathName());
}
