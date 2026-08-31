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
#include <string>

namespace
{

// A config written to a temp file and read back, which is the trip a setting actually makes.
std::string RoundTrip(const std::string &value)
{
	char path[L_tmpnam + 16];
	std::snprintf(path, sizeof path, "%s/zx_cfgtest_%d.ini", P_tmpdir, (int)value.size());

	{
		FConfigFile out;
		out.SetSection("GlobalSettings", true);
		out.SetValueForKey("some_cvar", value.c_str());
		out.ChangePathName(path);
		out.WriteConfigFile();
	}

	FConfigFile in(path);
	std::remove(path);

	if (!in.SetSection("GlobalSettings"))
		return "<no section>";

	const char *got = in.GetValueForKey("some_cvar");
	return got != NULL ? got : "<no key>";
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
	char path[L_tmpnam + 16];
	std::snprintf(path, sizeof path, "%s/zx_cfgtest_tail.ini", P_tmpdir);

	{
		FConfigFile out;
		out.SetSection("GlobalSettings", true);
		out.SetValueForKey("some_cvar", value.c_str());
		out.ChangePathName(path);
		out.WriteConfigFile();
	}

	FConfigFile in(path);
	std::remove(path);
	ASSERT_TRUE(in.SetSection("GlobalSettings"));

	int entries = 0;
	const char *key;
	const char *val;
	while (in.NextInSection(key, val))
		++entries;

	EXPECT_EQ(1, entries);
}
