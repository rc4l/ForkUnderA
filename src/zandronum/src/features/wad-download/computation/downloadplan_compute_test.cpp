// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/wad-download/computation/downloadplan_compute.h"

using zx::AssembleSiteOrder;
using zx::BuildCandidateUrls;
using zx::DownloadSourceName;
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

// ---------------------------------------------------------------- naming a download source

TEST(DownloadSourceName, NamesTheHostAndNothingElse)
{
	EXPECT_EQ("wads.example.net", DownloadSourceName("https://wads.example.net/doom/brutal.wad"));
	EXPECT_EQ("wads.example.net", DownloadSourceName("http://wads.example.net/"));
	EXPECT_EQ("wads.example.net", DownloadSourceName("wads.example.net/doom/brutal.wad"));
}

TEST(DownloadSourceName, KeepsThePortBecauseAServerServingItsOwnWadsIsIdentifiedByIt)
{
	// Two servers on one address are told apart only by port, and this line exists so a player can
	// see which machine a file came from.
	EXPECT_EQ("203.0.113.7:10666", DownloadSourceName("http://203.0.113.7:10666/wads/brutal.wad"));
}

TEST(DownloadSourceName, StripsCredentials)
{
	// [rc4l] The reason this is not just "print the URL". A player pastes a console log into a bug
	// report, and anything in it is published.
	EXPECT_EQ("mirror.example.org", DownloadSourceName("https://user:secret@mirror.example.org/x.wad"));
}

TEST(DownloadSourceName, StripsAQueryThatCouldCarryAToken)
{
	EXPECT_EQ("cdn.example.com", DownloadSourceName("https://cdn.example.com?token=abc123"));
	EXPECT_EQ("cdn.example.com", DownloadSourceName("https://cdn.example.com/a.wad?token=abc123"));
}

TEST(DownloadSourceName, FallsBackToTheInputRatherThanGoingBlank)
{
	// An odd cl_fua_downloadsites entry should still be nameable in the log.
	EXPECT_EQ("https:///a.wad", DownloadSourceName("https:///a.wad"));
	EXPECT_EQ("", DownloadSourceName(""));
}

//*****************************************************************************
// AssembleSiteOrder -- the download preference policy itself.

TEST(AssembleSiteOrder, ServerSitesThenMirrorsThenLastResort)
{
	// [rc4l] The regression this pins: joining a server used to put the HOSTING MACHINE's own
	// endpoint ahead of the public mirrors, so every download crawled over one residential upload
	// while six file hosts sat idle. The host belongs at the END -- still present (it is the one
	// source certain to have the file), but only reached once the mirrors have missed.
	const std::vector<std::string> server = { "https://operator.example/wads/" };
	const std::vector<std::string> mirrors = { "https://mirror-a.example/", "https://mirror-b.example/" };
	const std::vector<std::string> host = { "http://203.0.113.7:10666/" };

	const std::vector<std::string> order = AssembleSiteOrder(server, mirrors, host);

	ASSERT_EQ(4u, order.size());
	EXPECT_EQ("https://operator.example/wads/", order[0]);
	EXPECT_EQ("https://mirror-a.example/", order[1]);
	EXPECT_EQ("https://mirror-b.example/", order[2]);
	EXPECT_EQ("http://203.0.113.7:10666/", order[3]);
}

TEST(AssembleSiteOrder, EmptyGroupsJustDisappear)
{
	// A server that advertises nothing and serves nothing itself leaves exactly the mirror list.
	const std::vector<std::string> none;
	const std::vector<std::string> mirrors = { "https://mirror.example/" };

	EXPECT_EQ(mirrors, AssembleSiteOrder(none, mirrors, none));
	EXPECT_TRUE(AssembleSiteOrder(none, none, none).empty());
}

TEST(AssembleSiteOrder, LeavesDuplicatesForNormalizeToResolveInFavourOfTheEarlierSlot)
{
	// A site listed both by the server and as a last resort must keep its earlier (better) position
	// once NormalizeDownloadSites dedups -- which keeps the FIRST occurrence. This test documents
	// the division of labour: AssembleSiteOrder orders, Normalize dedups.
	const std::vector<std::string> server = { "https://both.example/" };
	const std::vector<std::string> none;
	const std::vector<std::string> tail = { "https://both.example/" };

	const std::vector<std::string> order = AssembleSiteOrder(server, none, tail);
	ASSERT_EQ(2u, order.size());

	const std::vector<std::string> deduped = NormalizeDownloadSites(order);
	ASSERT_EQ(1u, deduped.size());
	EXPECT_EQ("https://both.example/", deduped[0]);
}

//*****************************************************************************
// FormatDownloadStatus -- the progress line's three shapes and the boundaries between them.

TEST(FormatDownloadStatus, SaysSearchingUntilAnySourceAnswers)
{
	// No Content-Length and no bytes: source selection is still running (DNS, connect, mirrors
	// 404ing past). A bare 0% here would read as a stalled transfer.
	EXPECT_EQ("Searching for brutal.wad...", zx::FormatDownloadStatus("brutal.wad", 0, -1));
	EXPECT_EQ("Searching for brutal.wad...", zx::FormatDownloadStatus("brutal.wad", 0, 0));
}

TEST(FormatDownloadStatus, FlipsToPercentTheMomentContentLengthArrives)
{
	// The boundary the searching text hands over at: headers landed, nothing received yet -- this
	// is a real 0%, not a search.
	EXPECT_EQ("brutal.wad    0%  (  0 KB of 7.0 MB)",
		zx::FormatDownloadStatus("brutal.wad", 0, 7 * 1024 * 1024));
}

TEST(FormatDownloadStatus, PercentLineIsFixedWidthThroughTheTransfer)
{
	// The band behind the line is sized from it, so 5% and 50% and 100% must all be the same
	// length: percent padded to three columns, received padded to the total's width.
	const long long total = 10 * 1024 * 1024;
	const std::string early = zx::FormatDownloadStatus("a.wad", total / 20, total);
	const std::string mid = zx::FormatDownloadStatus("a.wad", total / 2, total);
	const std::string done = zx::FormatDownloadStatus("a.wad", total, total);

	EXPECT_EQ(early.size(), mid.size());
	EXPECT_EQ(mid.size(), done.size());
	EXPECT_EQ("a.wad   50%  ( 5.0 MB of 10.0 MB)", mid);
}

TEST(FormatDownloadStatus, ShowsBytesAloneWhenLengthIsUnknown)
{
	// Bytes flowing but no Content-Length: a percentage would be a guess.
	EXPECT_EQ("a.wad  512 KB", zx::FormatDownloadStatus("a.wad", 512 * 1024, -1));
}

TEST(HumanBytes, PicksTheUnitAndAnswersNegativesWithAQuestionMark)
{
	EXPECT_EQ("0 KB", zx::HumanBytes(0));
	EXPECT_EQ("512 KB", zx::HumanBytes(512 * 1024));
	EXPECT_EQ("1.5 MB", zx::HumanBytes(1536 * 1024));
	EXPECT_EQ("?", zx::HumanBytes(-1));
}

// MergeDownloadSites

namespace {
const std::vector<std::string> kOld = { "https://a/wads/", "https://b/wads/" };
const std::vector<std::string> kNew = { "https://a/wads/", "https://b/wads/", "https://c/wads/" };
}

TEST(MergeDownloadSites, GivesAStaleDefaultTheMirrorsAddedSinceItWasSaved)
{
	// The bug this exists for: an ini written before c/ was shipped never reaches c/.
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(kOld, std::vector<std::string>(), kNew);

	EXPECT_EQ(kNew, got.sites);
	EXPECT_TRUE(got.changed);
}

TEST(MergeDownloadSites, LeavesAMirrorOutWhenTheStampSaysItWasRemoved)
{
	// b/ is shipped and missing from their list, but the stamp proves they had it and took it out.
	const std::vector<std::string> saved = { "https://a/wads/" };
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(saved, kOld, kNew);

	const std::vector<std::string> want = { "https://a/wads/", "https://c/wads/" };
	EXPECT_EQ(want, got.sites);
}

TEST(MergeDownloadSites, KeepsTheirOwnMirrorsAndTheirOrder)
{
	// Appending only, so a hand-written list is never reordered or trimmed.
	const std::vector<std::string> saved = { "https://mine/", "https://b/wads/" };
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(saved, kOld, kNew);

	const std::vector<std::string> want = { "https://mine/", "https://b/wads/", "https://c/wads/" };
	EXPECT_EQ(want, got.sites);
}

TEST(MergeDownloadSites, SaysNothingChangedWhenTheListIsAlreadyCurrent)
{
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(kNew, kNew, kNew);

	EXPECT_EQ(kNew, got.sites);
	EXPECT_FALSE(got.changed);
}

TEST(MergeDownloadSites, TreatsCaseAndATrailingSlashAsTheSameMirror)
{
	// Spelled without its slash and shouted; appending it again would fetch the same host twice.
	const std::vector<std::string> saved = { "HTTPS://A/WADS", "https://b/wads/" };
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(saved, kNew, kNew);

	EXPECT_EQ(saved, got.sites);
	EXPECT_FALSE(got.changed);
}

TEST(MergeDownloadSites, SkipsAnEmptyShippedEntry)
{
	// Whitespace in the shipped list must not become an empty site to fetch.
	const std::vector<std::string> shipped = { "", "https://c/wads/" };
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(kOld, kOld, shipped);

	EXPECT_EQ(kNew, got.sites);
}

TEST(MergeDownloadSites, StartsFromTheShippedListWhenNothingIsSaved)
{
	const zx::DownloadSiteMerge got = zx::MergeDownloadSites(std::vector<std::string>(),
		std::vector<std::string>(), kNew);

	EXPECT_EQ(kNew, got.sites);
	EXPECT_TRUE(got.changed);
}

// The launch-to-launch cycle. MergeDownloadSites alone was tested and passed while the feature was
// broken on a real machine, because the value does not go from one launch to the next as a vector:
// it is joined into the CVAR, written to the ini, and split back. These tests run that whole cycle.

namespace {
std::vector<std::string> Relaunch(const std::vector<std::string> &sites)
{
	return zx::SplitOnWhitespace(zx::JoinDownloadSites(sites));
}

const std::vector<std::string> kShipped = {
	"https://static.allfearthesentinel.com/wads/", "https://euroboros.net/zandronum/wads/",
	"https://static.audrealms.org/wads/", "http://grandpachuck.org/files/wads/",
	"https://wads.doomleague.org/", "https://wads.firestick.games/",
	"https://static.action.fapnow.xyz/wads/" };
}

TEST(JoinDownloadSites, SurvivesTheRoundTripThroughTheCvar)
{
	EXPECT_EQ(kShipped, Relaunch(kShipped));
}

TEST(JoinDownloadSites, DropsAnEmptyEntryRatherThanDoublingASeparator)
{
	const std::vector<std::string> withHole = { "https://a/", "", "https://b/" };

	EXPECT_EQ("https://a/ https://b/", zx::JoinDownloadSites(withHole));
}

TEST(MergeDownloadSites, AddsAMirrorOnceAcrossRepeatedLaunches)
{
	// The bug this was written for: the mirror was appended again on every launch, and the list grew
	// a fresh copy each time. Three launches, and only the first may change anything.
	std::vector<std::string> saved = { "https://a/wads/", "https://b/wads/" };
	std::vector<std::string> stamp;

	const zx::DownloadSiteMerge first = zx::MergeDownloadSites(saved, stamp, kShipped);
	EXPECT_TRUE(first.changed);
	saved = Relaunch(first.sites);
	stamp = Relaunch(kShipped);

	for (int launch = 0; launch < 2; ++launch)
	{
		const zx::DownloadSiteMerge again = zx::MergeDownloadSites(saved, stamp, kShipped);

		EXPECT_FALSE(again.changed) << "launch " << launch << " appended something twice";
		EXPECT_EQ(saved, again.sites);
		saved = Relaunch(again.sites);
	}
}

TEST(MergeDownloadSites, KeepsARemovedMirrorOutAcrossRepeatedLaunches)
{
	// A player deletes one, and it must stay deleted however many times they launch.
	std::vector<std::string> saved = kShipped;
	saved.pop_back();
	const std::vector<std::string> stamp = Relaunch(kShipped);

	for (int launch = 0; launch < 3; ++launch)
	{
		const zx::DownloadSiteMerge got = zx::MergeDownloadSites(saved, stamp, kShipped);

		EXPECT_FALSE(got.changed);
		EXPECT_EQ(saved, got.sites);
		saved = Relaunch(got.sites);
	}
}
