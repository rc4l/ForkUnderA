// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-download/computation/downloadplan_compute.h"

using zx::BuildCandidateUrls;
using zx::IsSafeDownloadName;
using zx::NormalizeDownloadSites;
using zx::SplitOnWhitespace;
using zx::UrlEscapeFileName;
using std::string;
using std::vector;

namespace
{
bool Contains(const vector<string> &haystack, const string &needle)
{
	for (size_t i = 0; i < haystack.size(); ++i)
	{
		if (haystack[i] == needle)
			return true;
	}
	return false;
}
} // namespace

//=============================================================================
// SplitOnWhitespace -- the CVAR list format
//=============================================================================

TEST(SplitOnWhitespace, SplitsASpaceSeparatedList)
{
	const vector<string> got = SplitOnWhitespace("https://a/ https://b/");
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("https://a/", got[0]);
	EXPECT_EQ("https://b/", got[1]);
}

TEST(SplitOnWhitespace, CollapsesRunsAndTrimsEnds)
{
	// A CVAR edited by hand collects stray spacing; that must not produce empty sites to fetch.
	const vector<string> got = SplitOnWhitespace("  a\t\tb \n c  ");
	ASSERT_EQ(3u, got.size());
	EXPECT_EQ("a", got[0]);
	EXPECT_EQ("b", got[1]);
	EXPECT_EQ("c", got[2]);
}

TEST(SplitOnWhitespace, AnEmptyOrBlankValueYieldsNothing)
{
	EXPECT_TRUE(SplitOnWhitespace("").empty());
	EXPECT_TRUE(SplitOnWhitespace("   \t\n ").empty());
}

//=============================================================================
// NormalizeDownloadSites
//=============================================================================

TEST(NormalizeDownloadSites, AddsTheTrailingSlashTheUrlBuilderAssumes)
{
	const vector<string> got = NormalizeDownloadSites({ "https://example.com/wads" });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("https://example.com/wads/", got[0]);
}

TEST(NormalizeDownloadSites, LeavesAQueryEndpointEndingInEqualsAlone)
{
	// A mirror can be a search endpoint where the name is a query VALUE. Appending '/' there would
	// ask for a file literally named "/brutal.wad".
	const vector<string> got = NormalizeDownloadSites({ "https://example.com/getwad.php?search=" });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("https://example.com/getwad.php?search=", got[0]);
}

TEST(NormalizeDownloadSites, KeepsPlainHttpBecauseMostWadMirrorsAreHttpOnly)
{
	const vector<string> got = NormalizeDownloadSites({ "http://example.com/wads/" });
	ASSERT_EQ(1u, got.size());
	EXPECT_EQ("http://example.com/wads/", got[0]);
}

TEST(NormalizeDownloadSites, DropsEntriesThatAreNotFetchableUrls)
{
	// Junk in the list is skipped rather than failing the whole download: the list is user-edited and
	// one bad line should not cost them every other mirror.
	const vector<string> got = NormalizeDownloadSites({
		"", "example.com/wads/", "ftp://example.com/", "file:///etc/passwd", "https://" });
	EXPECT_TRUE(got.empty());
}

TEST(NormalizeDownloadSites, DropsLaterDuplicatesKeepingPreferenceOrder)
{
	const vector<string> got = NormalizeDownloadSites({
		"https://a.example/", "https://b.example/", "https://a.example" });
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("https://a.example/", got[0]);
	EXPECT_EQ("https://b.example/", got[1]);
}

//=============================================================================
// UrlEscapeFileName -- a server-chosen name must not restructure the URL
//=============================================================================

TEST(UrlEscapeFileName, LeavesAnOrdinaryWadNameUntouched)
{
	EXPECT_EQ("brutal_v21.wad", UrlEscapeFileName("brutal_v21.wad"));
	EXPECT_EQ("map-01.pk3", UrlEscapeFileName("map-01.pk3"));
}

TEST(UrlEscapeFileName, EscapesTheCharactersThatWouldSplitAUrl)
{
	EXPECT_EQ("a%3Fb.wad", UrlEscapeFileName("a?b.wad"));
	EXPECT_EQ("a%23b.wad", UrlEscapeFileName("a#b.wad"));
	EXPECT_EQ("a%2Fb.wad", UrlEscapeFileName("a/b.wad"));
	EXPECT_EQ("a%20b.wad", UrlEscapeFileName("a b.wad"));
	EXPECT_EQ("a%26b.wad", UrlEscapeFileName("a&b.wad"));
}

TEST(UrlEscapeFileName, EscapesHighBytesRatherThanEmittingThemRaw)
{
	const string got = UrlEscapeFileName("\xE9.wad");
	EXPECT_EQ("%E9.wad", got);
}

//=============================================================================
// IsSafeDownloadName -- the file we are about to create on the player's disk
//=============================================================================

TEST(IsSafeDownloadName, AcceptsTheResourceTypesTheEngineLoads)
{
	EXPECT_TRUE(IsSafeDownloadName("brutal.wad"));
	EXPECT_TRUE(IsSafeDownloadName("skins.pk3"));
	EXPECT_TRUE(IsSafeDownloadName("music.pk7"));
	EXPECT_TRUE(IsSafeDownloadName("maps.zip"));
	EXPECT_TRUE(IsSafeDownloadName("patch.deh"));
	EXPECT_TRUE(IsSafeDownloadName("patch.bex"));
	EXPECT_TRUE(IsSafeDownloadName("MIXED.Wad"));
}

TEST(IsSafeDownloadName, RejectsPathSeparatorsSoNothingEscapesTheDownloadDirectory)
{
	EXPECT_FALSE(IsSafeDownloadName("sub/brutal.wad"));
	EXPECT_FALSE(IsSafeDownloadName("sub\\brutal.wad"));
	EXPECT_FALSE(IsSafeDownloadName("/etc/brutal.wad"));
	EXPECT_FALSE(IsSafeDownloadName("C:brutal.wad"));
	EXPECT_FALSE(IsSafeDownloadName("C:\\windows\\brutal.wad"));
}

TEST(IsSafeDownloadName, RejectsTraversal)
{
	EXPECT_FALSE(IsSafeDownloadName("..\\..\\autoexec.wad"));
	EXPECT_FALSE(IsSafeDownloadName("../brutal.wad"));
	// No separator, but still contains "..": refused rather than reasoned about.
	EXPECT_FALSE(IsSafeDownloadName("a..b.wad"));
}

TEST(IsSafeDownloadName, RejectsExtensionsTheEngineWouldNotLoad)
{
	// A download is a file a remote host asked us to create. There is no reason for it to be any of
	// these, and every reason not to write them next to the executable.
	EXPECT_FALSE(IsSafeDownloadName("evil.exe"));
	EXPECT_FALSE(IsSafeDownloadName("evil.dll"));
	EXPECT_FALSE(IsSafeDownloadName("evil.bat"));
	EXPECT_FALSE(IsSafeDownloadName("autoexec.cfg"));
	EXPECT_FALSE(IsSafeDownloadName("brutal"));
	EXPECT_FALSE(IsSafeDownloadName("brutal.wad.exe"));
}

TEST(IsSafeDownloadName, RejectsControlCharactersAndEmbeddedNewlines)
{
	EXPECT_FALSE(IsSafeDownloadName(string("bru\0tal.wad", 11)));
	EXPECT_FALSE(IsSafeDownloadName("bru\ntal.wad"));
	EXPECT_FALSE(IsSafeDownloadName("bru\ttal.wad"));
}

TEST(IsSafeDownloadName, RejectsWindowsReservedDeviceStemsOnEveryPlatform)
{
	// Win32 resolves these to devices whatever the extension, so "con.wad" is not a file. Refused
	// everywhere so the behaviour does not depend on which machine the player is on.
	EXPECT_FALSE(IsSafeDownloadName("con.wad"));
	EXPECT_FALSE(IsSafeDownloadName("NUL.wad"));
	EXPECT_FALSE(IsSafeDownloadName("com1.wad"));
	EXPECT_FALSE(IsSafeDownloadName("LPT9.pk3"));
	// ...but a name that merely starts with one is an ordinary file.
	EXPECT_TRUE(IsSafeDownloadName("console.wad"));
	EXPECT_TRUE(IsSafeDownloadName("com10.wad"));
}

TEST(IsSafeDownloadName, RejectsNamesWin32WouldSilentlyRewriteOnCreate)
{
	// Win32 strips a trailing space or dot, so the name we validated would not be the name on disk.
	EXPECT_FALSE(IsSafeDownloadName("brutal.wad "));
	EXPECT_FALSE(IsSafeDownloadName("brutal.wad."));
}

TEST(IsSafeDownloadName, RejectsEmptyHiddenAndAbsurdlyLongNames)
{
	EXPECT_FALSE(IsSafeDownloadName(""));
	EXPECT_FALSE(IsSafeDownloadName(".hidden.wad"));
	EXPECT_FALSE(IsSafeDownloadName(string(200, 'a') + ".wad"));
}

//=============================================================================
// BuildCandidateUrls
//=============================================================================

TEST(BuildCandidateUrls, TriesThreeSpellingsPerSiteBecauseMirrorsAreCaseSensitive)
{
	const vector<string> got = BuildCandidateUrls({ "https://m.example/wads/" }, "Brutal.wad");
	ASSERT_EQ(3u, got.size());
	EXPECT_EQ("https://m.example/wads/Brutal.wad", got[0]);
	EXPECT_EQ("https://m.example/wads/brutal.wad", got[1]);
	EXPECT_EQ("https://m.example/wads/BRUTAL.WAD", got[2]);
}

TEST(BuildCandidateUrls, EmitsCoincidingSpellingsOnce)
{
	// An already-lowercase name has only two distinct spellings, so it must not cost three requests.
	const vector<string> got = BuildCandidateUrls({ "https://m.example/wads/" }, "brutal.wad");
	ASSERT_EQ(2u, got.size());
	EXPECT_EQ("https://m.example/wads/brutal.wad", got[0]);
	EXPECT_EQ("https://m.example/wads/BRUTAL.WAD", got[1]);
}

TEST(BuildCandidateUrls, ExhaustsOneMirrorBeforeMovingToTheNext)
{
	// Walking every site per spelling instead would triple the round trips to the least-preferred
	// mirrors before ever finishing with the most-preferred one.
	const vector<string> got = BuildCandidateUrls(
		{ "https://first.example/", "https://second.example/" }, "Mod.wad");
	ASSERT_EQ(6u, got.size());
	EXPECT_EQ("https://first.example/Mod.wad", got[0]);
	EXPECT_EQ("https://first.example/mod.wad", got[1]);
	EXPECT_EQ("https://first.example/MOD.WAD", got[2]);
	EXPECT_EQ("https://second.example/Mod.wad", got[3]);
}

TEST(BuildCandidateUrls, AppendsToAQueryEndpointWithoutAddingAPathSegment)
{
	const vector<string> got = BuildCandidateUrls(
		{ "https://m.example/getwad.php?search=" }, "brutal.wad");
	EXPECT_TRUE(Contains(got, "https://m.example/getwad.php?search=brutal.wad"));
}

TEST(BuildCandidateUrls, EscapesTheNameItPastesIn)
{
	const vector<string> got = BuildCandidateUrls({ "https://m.example/wads/" }, "od d.wad");
	ASSERT_FALSE(got.empty());
	EXPECT_EQ("https://m.example/wads/od%20d.wad", got[0]);
}

TEST(BuildCandidateUrls, RefusesAnUnsafeNameOutrightRatherThanEscapingItIntoSafety)
{
	// Escaping would make "../../x.wad" a valid URL to fetch -- and then we would still have to name
	// the local file. The name is rejected at the plan stage so no request is ever built.
	EXPECT_TRUE(BuildCandidateUrls({ "https://m.example/" }, "../../evil.wad").empty());
	EXPECT_TRUE(BuildCandidateUrls({ "https://m.example/" }, "evil.exe").empty());
}

TEST(BuildCandidateUrls, YieldsNothingWhenNoSiteSurvivesNormalisation)
{
	EXPECT_TRUE(BuildCandidateUrls({ "not a url", "ftp://x/" }, "brutal.wad").empty());
	EXPECT_TRUE(BuildCandidateUrls({}, "brutal.wad").empty());
}
