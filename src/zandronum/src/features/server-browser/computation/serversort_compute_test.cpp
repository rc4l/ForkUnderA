// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/serversort_compute.h"

using zx::CompareServers;

// [rc4l] Named so a row of bare true/false does not read as a puzzle at the call site.
const bool kLan = true;
const bool kNet = false;
using zx::ServerSortKey;
using std::string;

namespace
{
const string ESC(1, '\034');
} // namespace

// ---------------------------------------------------------------- the sort key

TEST(ServerSortKey, FoldsCase)
{
	// ASCII puts every capital before every lowercase letter, which files "Brutal" and "brutal" in
	// completely different places.
	EXPECT_EQ("brutal doom", ServerSortKey("Brutal Doom"));
	EXPECT_EQ(ServerSortKey("BRUTAL"), ServerSortKey("brutal"));
}

TEST(ServerSortKey, StripsSingleCharacterColourCodes)
{
	// Raw bytes sort a coloured name under 0x1C -- before everything, for no reason the player can
	// see on screen.
	EXPECT_EQ("colourful server", ServerSortKey(ESC + "dColourful " + ESC + "hServer"));
}

TEST(ServerSortKey, StripsBracketedColourCodes)
{
	EXPECT_EQ("gold text", ServerSortKey(ESC + "[Gold]Gold " + ESC + "[Red]Text"));
}

TEST(ServerSortKey, SortsAColouredNameWhereItsLettersSay)
{
	// The point of the whole thing: colour must not change position.
	EXPECT_EQ(ServerSortKey("Alpha"), ServerSortKey(ESC + "dAlpha"));
}

TEST(ServerSortKey, SurvivesTruncatedCodes)
{
	// A name cut mid-escape is something a server can send, deliberately or not.
	EXPECT_EQ("ab", ServerSortKey("ab" + ESC));
	EXPECT_EQ("", ServerSortKey(ESC + "[unterminated"));
}

TEST(ServerSortKey, LeavesAPlainNameAlone)
{
	EXPECT_EQ("dwango5", ServerSortKey("dwango5"));
	EXPECT_EQ("", ServerSortKey(""));
}

// ---------------------------------------------------------------- the order

TEST(CompareServers, PutsMorePlayersFirst)
{
	EXPECT_LT(CompareServers(kNet, 11, "zzz", kNet, 2, "aaa"), 0);
	EXPECT_GT(CompareServers(kNet, 2, "aaa", kNet, 11, "zzz"), 0);
}

TEST(CompareServers, KeepsFullServersAtTheTopRatherThanBuryingThem)
{
	// Deliberate: a full server is evidence about where people play, and the player count already
	// reads red. Sorting it below an empty one answers a question nobody asked.
	EXPECT_LT(CompareServers(kNet, 32, "packed", kNet, 0, "empty"), 0);
}

TEST(CompareServers, BreaksTiesAlphabetically)
{
	EXPECT_LT(CompareServers(kNet, 4, "alpha", kNet, 4, "beta"), 0);
	EXPECT_GT(CompareServers(kNet, 4, "beta", kNet, 4, "alpha"), 0);
}

TEST(CompareServers, BreaksTiesRegardlessOfCaseOrColour)
{
	EXPECT_LT(CompareServers(kNet, 4, "Alpha", kNet, 4, "beta"), 0);
	EXPECT_LT(CompareServers(kNet, 4, ESC + "dAlpha", kNet, 4, "beta"), 0);
	EXPECT_GT(CompareServers(kNet, 4, ESC + "dzulu", kNet, 4, "Beta"), 0);
}

TEST(CompareServers, TiesCompletelyOnIdenticalServers)
{
	EXPECT_EQ(0, CompareServers(kNet, 4, "same", kNet, 4, "same"));
	EXPECT_EQ(0, CompareServers(kNet, 4, "Same", kNet, 4, ESC + "dsame"));
}

// ---------------------------------------------------------------- the LAN group

TEST(CompareServers, PutsLanAboveTheInternetEvenWhenItIsEmpty)
{
	// [rc4l] THE POINT OF THE RULE, and the case a mere weighting would get wrong. An empty server on
	// your own network still outranks a packed one across the internet: it is the one you just started
	// and the one the person next to you is on, and no player count changes which of those you meant.
	EXPECT_LT(CompareServers(kLan, 0, "empty", kNet, 32, "packed"), 0);
	EXPECT_GT(CompareServers(kNet, 32, "packed", kLan, 0, "empty"), 0);
}

TEST(CompareServers, SortsWithinTheLanGroupExactlyAsBefore)
{
	// LAN is a group, not a replacement for the ordering. Inside it, everything below still holds.
	EXPECT_LT(CompareServers(kLan, 11, "zzz", kLan, 2, "aaa"), 0);
	EXPECT_LT(CompareServers(kLan, 4, "alpha", kLan, 4, "beta"), 0);
	EXPECT_LT(CompareServers(kLan, 4, "Alpha", kLan, 4, "beta"), 0);
	EXPECT_EQ(0, CompareServers(kLan, 4, "same", kLan, 4, "same"));
}

TEST(CompareServers, LeavesTheInternetGroupAlone)
{
	// The other half of the same statement: nothing about the internet ordering changed, so a list
	// with no LAN server on it looks exactly as it did.
	EXPECT_LT(CompareServers(kNet, 11, "zzz", kNet, 2, "aaa"), 0);
	EXPECT_LT(CompareServers(kNet, 4, "alpha", kNet, 4, "beta"), 0);
}

TEST(CompareServers, DoesNotTieALanServerWithAnIdenticalInternetOne)
{
	// Same name, same population, different network. Returning 0 here would let qsort put them in
	// either order, which is the one outcome the rule exists to rule out.
	EXPECT_LT(CompareServers(kLan, 4, "same", kNet, 4, "same"), 0);
	EXPECT_GT(CompareServers(kNet, 4, "same", kLan, 4, "same"), 0);
}

TEST(CompareServers, IsAntisymmetric)
{
	// A comparator that disagrees with itself makes qsort's behaviour undefined, which shows up as a
	// list that reorders on every refresh rather than as a crash.
	// [rc4l] Both LAN and internet rows, because the LAN rule is a whole extra dimension for the
	// ordering to be inconsistent in -- and the pairs that would expose it are exactly the ones where
	// LAN and population disagree.
	struct Row { bool lan; int players; const char *name; };
	const Row rows[] = {
		{ kNet, 0, "empty" }, { kNet, 4, "alpha" }, { kNet, 4, "Beta" }, { kNet, 32, "packed" },
		{ kNet, 1, "\034dcoloured" }, { kNet, 4, "alpha" }, { kNet, 7, "zeta" },
		{ kLan, 0, "empty" }, { kLan, 4, "alpha" }, { kLan, 32, "packed" },
	};
	const int count = static_cast<int>( sizeof(rows) / sizeof(rows[0]) );

	for (int i = 0; i < count; ++i)
	{
		for (int j = 0; j < count; ++j)
		{
			const int ab = CompareServers(rows[i].lan, rows[i].players, rows[i].name,
				rows[j].lan, rows[j].players, rows[j].name);
			const int ba = CompareServers(rows[j].lan, rows[j].players, rows[j].name,
				rows[i].lan, rows[i].players, rows[i].name);

			if (ab == 0)
				EXPECT_EQ(0, ba) << i << " vs " << j;
			else
				EXPECT_TRUE((ab < 0) != (ba < 0)) << i << " vs " << j;
		}
	}
}

TEST(CompareServers, IsTransitive)
{
	struct Row { bool lan; int players; const char *name; };
	const Row rows[] = {
		{ kNet, 0, "empty" }, { kNet, 4, "alpha" }, { kNet, 4, "Beta" }, { kNet, 32, "packed" },
		{ kNet, 7, "zeta" },
		{ kLan, 0, "empty" }, { kLan, 4, "alpha" }, { kLan, 32, "packed" },
	};
	const int count = static_cast<int>( sizeof(rows) / sizeof(rows[0]) );

	for (int i = 0; i < count; ++i)
		for (int j = 0; j < count; ++j)
			for (int k = 0; k < count; ++k)
			{
				const int ij = CompareServers(rows[i].lan, rows[i].players, rows[i].name,
					rows[j].lan, rows[j].players, rows[j].name);
				const int jk = CompareServers(rows[j].lan, rows[j].players, rows[j].name,
					rows[k].lan, rows[k].players, rows[k].name);
				const int ik = CompareServers(rows[i].lan, rows[i].players, rows[i].name,
					rows[k].lan, rows[k].players, rows[k].name);

				if ((ij < 0) && (jk < 0))
					EXPECT_LT(ik, 0) << i << "," << j << "," << k;
			}
}

// ---------------------------------------------------------------- version ordering
//
// [rc4l] A server the player cannot act on sinks within its group. Older and Unknown do; Same and
// Newer do not, because an update reaches a newer one and nothing reaches an older one.

using zx::CompareServersWithVersion;
using zx::VersionRelation;

TEST(ServerSort, AnOlderServerSinksBelowAJoinableOne)
{
	// Even though it has more players, which would otherwise put it first.
	EXPECT_GT(CompareServersWithVersion(false, 9, "busy", VersionRelation::Older,
	                                    false, 1, "quiet", VersionRelation::Same), 0);
}

TEST(ServerSort, ANewerServerKeepsItsPlace)
{
	// One update away, so it is ranked on population like anything else.
	EXPECT_LT(CompareServersWithVersion(false, 9, "busy", VersionRelation::Newer,
	                                    false, 1, "quiet", VersionRelation::Same), 0);
}

TEST(ServerSort, AnUnreadableVersionSinksLikeAnOldOne)
{
	EXPECT_GT(CompareServersWithVersion(false, 9, "busy", VersionRelation::Unknown,
	                                    false, 1, "quiet", VersionRelation::Same), 0);
}

TEST(ServerSort, SinkingHappensInsideTheGroupNotAcrossIt)
{
	// A LAN server on an old build is still on your network, so it stays above every internet
	// server -- the same reasoning that made LAN a group rather than a score.
	EXPECT_LT(CompareServersWithVersion(true, 0, "old lan", VersionRelation::Older,
	                                    false, 32, "busy internet", VersionRelation::Same), 0);
}

TEST(ServerSort, TwoSinkingServersStillSortAgainstEachOther)
{
	// Sinking is a group, not a tie: within it the usual rules apply.
	EXPECT_LT(CompareServersWithVersion(false, 9, "busy", VersionRelation::Older,
	                                    false, 1, "quiet", VersionRelation::Older), 0);
}

TEST(ServerSort, WithoutAMismatchTheOrderIsUnchanged)
{
	// The version rule must not disturb the ordering everything else relies on.
	EXPECT_EQ(CompareServersWithVersion(false, 5, "a", VersionRelation::Same,
	                                    false, 5, "b", VersionRelation::Same),
	          zx::CompareServers(false, 5, "a", false, 5, "b"));
}
