// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/serversort_compute.h"

using zx::CompareServers;
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
	EXPECT_LT(CompareServers(11, "zzz", 2, "aaa"), 0);
	EXPECT_GT(CompareServers(2, "aaa", 11, "zzz"), 0);
}

TEST(CompareServers, KeepsFullServersAtTheTopRatherThanBuryingThem)
{
	// Deliberate: a full server is evidence about where people play, and the player count already
	// reads red. Sorting it below an empty one answers a question nobody asked.
	EXPECT_LT(CompareServers(32, "packed", 0, "empty"), 0);
}

TEST(CompareServers, BreaksTiesAlphabetically)
{
	EXPECT_LT(CompareServers(4, "alpha", 4, "beta"), 0);
	EXPECT_GT(CompareServers(4, "beta", 4, "alpha"), 0);
}

TEST(CompareServers, BreaksTiesRegardlessOfCaseOrColour)
{
	EXPECT_LT(CompareServers(4, "Alpha", 4, "beta"), 0);
	EXPECT_LT(CompareServers(4, ESC + "dAlpha", 4, "beta"), 0);
	EXPECT_GT(CompareServers(4, ESC + "dzulu", 4, "Beta"), 0);
}

TEST(CompareServers, TiesCompletelyOnIdenticalServers)
{
	EXPECT_EQ(0, CompareServers(4, "same", 4, "same"));
	EXPECT_EQ(0, CompareServers(4, "Same", 4, ESC + "dsame"));
}

TEST(CompareServers, IsAntisymmetric)
{
	// A comparator that disagrees with itself makes qsort's behaviour undefined, which shows up as a
	// list that reorders on every refresh rather than as a crash.
	struct Row { int players; const char *name; };
	const Row rows[] = {
		{ 0, "empty" }, { 4, "alpha" }, { 4, "Beta" }, { 32, "packed" },
		{ 1, "\034dcoloured" }, { 4, "alpha" }, { 7, "zeta" },
	};
	const int count = static_cast<int>( sizeof(rows) / sizeof(rows[0]) );

	for (int i = 0; i < count; ++i)
	{
		for (int j = 0; j < count; ++j)
		{
			const int ab = CompareServers(rows[i].players, rows[i].name, rows[j].players, rows[j].name);
			const int ba = CompareServers(rows[j].players, rows[j].name, rows[i].players, rows[i].name);

			if (ab == 0)
				EXPECT_EQ(0, ba) << i << " vs " << j;
			else
				EXPECT_TRUE((ab < 0) != (ba < 0)) << i << " vs " << j;
		}
	}
}

TEST(CompareServers, IsTransitive)
{
	struct Row { int players; const char *name; };
	const Row rows[] = {
		{ 0, "empty" }, { 4, "alpha" }, { 4, "Beta" }, { 32, "packed" }, { 7, "zeta" },
	};
	const int count = static_cast<int>( sizeof(rows) / sizeof(rows[0]) );

	for (int i = 0; i < count; ++i)
		for (int j = 0; j < count; ++j)
			for (int k = 0; k < count; ++k)
			{
				const int ij = CompareServers(rows[i].players, rows[i].name, rows[j].players, rows[j].name);
				const int jk = CompareServers(rows[j].players, rows[j].name, rows[k].players, rows[k].name);
				const int ik = CompareServers(rows[i].players, rows[i].name, rows[k].players, rows[k].name);

				if ((ij < 0) && (jk < 0))
					EXPECT_LT(ik, 0) << i << "," << j << "," << k;
			}
}
