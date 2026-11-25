// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: budget_compute_test.cpp
//
// Description: Tests for budget_compute: the packet room, frame budget and
// chunk limit at their floors, the cosmetic event bank's refill, order, fit
// and overflow, and the chunk cut over estimated record starts.
//
//-----------------------------------------------------------------------------

#include "budget_compute.h"
#include <gtest/gtest.h>

#include <string.h>
#include <stdlib.h>		// qsort

using namespace zx::netsnapshot;

// [Kaori-Chani] The random frames draw from this fixed seed, so every compiler tests the same
// inputs.
#define BUDGETTEST_SEED		0x9E3779B9u

static DWORD budgettest_Next(DWORD& s)
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

// [Kaori-Chani] The packet room and the chunk limit, each at its floor of 64 and just past it.
TEST(SnapshotBudget, PacketRoomAndChunkLimitHaveAFloorOf64)
{
	static const struct { unsigned int maxPacket, room; } rooms[] =
	{
		{ 0, 64 },
		{ SNAP_PACKET_OVERHEAD + 64, 64 },			// the room would be exactly 64
		{ SNAP_PACKET_OVERHEAD + 65, 65 },
		{ 1550, 1550 - SNAP_PACKET_OVERHEAD },
	};
	for (unsigned int i = 0; i < sizeof(rooms) / sizeof(rooms[0]); ++i)
	{
		EXPECT_EQ(rooms[i].room, ComputePacketRoom(rooms[i].maxPacket))
			<< "packet " << rooms[i].maxPacket;
		EXPECT_EQ(rooms[i].room, ComputeChunkLimit(rooms[i].maxPacket))
			<< "packet " << rooms[i].maxPacket;
	}
}

TEST(SnapshotBudget, TheFrameBudgetHasAFloorOf64)
{
	EXPECT_EQ(64, ComputeEffectiveBudget(-5));
	EXPECT_EQ(64, ComputeEffectiveBudget(64));
	EXPECT_EQ(65, ComputeEffectiveBudget(65));
	EXPECT_EQ(4500, ComputeEffectiveBudget(4500));
}

// [Kaori-Chani] Four tics' worth at most, or the pump's own tics where more; a rate of 0 holds
// nothing.
TEST(SnapshotBudget, TheCosmeticBankRefillsToFourTics)
{
	EXPECT_EQ(600, ComputeCosBankRefill(0, 600, 1));
	EXPECT_EQ(2400, ComputeCosBankRefill(0, 600, 4));
	EXPECT_EQ(2400, ComputeCosBankRefill(2400, 600, 1));
	EXPECT_EQ(3600, ComputeCosBankRefill(0, 600, 6));
	EXPECT_EQ(2400, ComputeCosBankRefill(3600, 600, 1));
	EXPECT_EQ(0, ComputeCosBankRefill(1000, 0, 3));
}

// [Kaori-Chani] Fills four picks, nearest first; at equal distance the carried event first.
static void budgettest_Picks(SnapCosPick picks[4])
{
	memset(picks, 0, sizeof(SnapCosPick) * 4);
	picks[0].dist = 300; picks[0].len = 50;  picks[0].index = 0;
	picks[1].dist = 100; picks[1].len = 300; picks[1].index = 1;
	picks[2].dist = 100; picks[2].len = 100; picks[2].index = 2; picks[2].carried = 1;
	picks[3].dist = 900; picks[3].len = 20;  picks[3].index = 3;
	qsort(picks, 4, sizeof(SnapCosPick), ComputeCosPickCmp);
}

TEST(SnapshotBudget, CosmeticPicksSortNearestFirstAndCarriedAhead)
{
	SnapCosPick picks[4];
	budgettest_Picks(picks);
	EXPECT_EQ(2u, picks[0].index);
	EXPECT_EQ(1u, picks[1].index);
	EXPECT_EQ(0u, picks[2].index);
	EXPECT_EQ(3u, picks[3].index);
}

// [Kaori-Chani] Lengths 100, 300, 50, 20: at 350 the 300 stops the prefix, 400 fits two exactly
// and 470 all four.
TEST(SnapshotBudget, CosmeticFitTakesThePrefixThatFits)
{
	SnapCosPick picks[4];
	budgettest_Picks(picks);
	unsigned int sent = 99;

	EXPECT_EQ(1u, ComputeCosFit(picks, 4, 350, sent));
	EXPECT_EQ(100u, sent);

	EXPECT_EQ(2u, ComputeCosFit(picks, 4, 400, sent));
	EXPECT_EQ(400u, sent);

	EXPECT_EQ(4u, ComputeCosFit(picks, 4, 470, sent));
	EXPECT_EQ(470u, sent);

	EXPECT_EQ(0u, ComputeCosFit(picks, 4, 0, sent));
	EXPECT_EQ(0u, sent);
}

// [Kaori-Chani] A capped pump's overflow, past a fit of two, against 400 bytes of room.
TEST(SnapshotBudget, CosmeticOverflowNamesWhyEachEventWasHeld)
{
	static const struct { unsigned int len; BYTE carried; BYTE want; } rows[] =
	{
		{ 50, 0, 0xEE },						// kept: left alone
		{ 50, 0, 0xEE },
		{ 200, 0, SNAP_COSO_CARRY },
		{ 10, 1, SNAP_COSO_CARRIED_AGAIN },		// carried once already, whatever its size
		{ 250, 0, SNAP_COSO_NO_ROOM },			// 450 would pass 400
		{ 200, 0, SNAP_COSO_CARRY },			// the walk goes on: exactly 400
		{ 1, 0, SNAP_COSO_NO_ROOM },
	};
	const unsigned int n = sizeof(rows) / sizeof(rows[0]);
	SnapCosPick picks[n];
	BYTE verdicts[n];
	memset(picks, 0, sizeof(picks));
	memset(verdicts, 0xEE, sizeof(verdicts));
	for (unsigned int k = 0; k < n; ++k)
	{
		picks[k].len = rows[k].len;
		picks[k].carried = rows[k].carried;
	}

	ComputeCosOverflow(picks, n, 2, 400, verdicts);
	for (unsigned int k = 0; k < n; ++k)
		EXPECT_EQ(rows[k].want, verdicts[k]) << "row " << k;

	// [Kaori-Chani] Nothing past the fit, or no room at all.
	BYTE none[n];
	memset(none, 0xEE, sizeof(none));
	ComputeCosOverflow(picks, n, n, 400, none);
	for (unsigned int k = 0; k < n; ++k)
		EXPECT_EQ(0xEE, none[k]) << "row " << k << " with nothing past the fit";

	ComputeCosOverflow(picks, n, 2, 0, none);
	EXPECT_EQ(SNAP_COSO_NO_ROOM, none[2]);
	EXPECT_EQ(SNAP_COSO_CARRIED_AGAIN, none[3]);
}

// [Kaori-Chani] Three 10-byte records. 0 or the whole payload: one chunk.
TEST(SnapshotBudget, AChunkCutOfAWholePayloadIsOneChunk)
{
	unsigned int cuts[8];
	static const unsigned int even[] = { 0, 10, 20, 30 };
	EXPECT_EQ(1u, ComputeChunkCuts(even, 4, 0, cuts));
	EXPECT_EQ(1u, ComputeChunkCuts(even, 4, 30, cuts));
	EXPECT_EQ(0u, cuts[0]);
}

// [Kaori-Chani] 21 cuts at 20 and holds two; 20 cuts at 19 and holds one, since the first record
// of a chunk carries its id raw.
TEST(SnapshotBudget, AChunkCutHoldsAsManyRecordsAsFit)
{
	unsigned int cuts[8];
	static const unsigned int even[] = { 0, 10, 20, 30 };

	EXPECT_EQ(2u, ComputeChunkCuts(even, 4, 21, cuts));
	EXPECT_EQ(0u, cuts[0]);
	EXPECT_EQ(2u, cuts[1]);

	EXPECT_EQ(3u, ComputeChunkCuts(even, 4, 20, cuts));
	EXPECT_EQ(1u, cuts[1]);
	EXPECT_EQ(2u, cuts[2]);
}

// [Kaori-Chani] A 100-byte record against 50-byte chunks goes alone, and the walk moves on.
TEST(SnapshotBudget, AnOversizedRecordTakesAChunkOfItsOwn)
{
	unsigned int cuts[8];
	static const unsigned int wide[] = { 0, 5, 105, 110 };
	EXPECT_EQ(3u, ComputeChunkCuts(wide, 4, 50, cuts));
	EXPECT_EQ(0u, cuts[0]);
	EXPECT_EQ(1u, cuts[1]);
	EXPECT_EQ(2u, cuts[2]);
}

// [Kaori-Chani] Random frames: chunks open at 0, ascend, cover every record, and stop only where
// the next record would pass chunkBytes - 1.
TEST(SnapshotBudget, ChunkCutsCoverEveryRecordOnRandomFrames)
{
	DWORD seed = BUDGETTEST_SEED;
	unsigned int bounds[65], cutsR[64];
	int bad = 0;
	int firstBad = -1;
	for (int sample = 0; sample < 20000; ++sample)
	{
		const unsigned int n = 1 + budgettest_Next(seed) % 64;
		bounds[0] = 0;
		for (unsigned int r = 0; r < n; ++r)
			bounds[r + 1] = bounds[r] + 1 + budgettest_Next(seed) % 120;
		const unsigned int chunkBytes = 20 + budgettest_Next(seed) % 400;
		const unsigned int est = bounds[n];
		const unsigned int cut = (chunkBytes >= est) ? est : chunkBytes - 1;
		const unsigned int nc = ComputeChunkCuts(bounds, n + 1, chunkBytes, cutsR);
		bool ok = (nc >= 1) && (nc <= n) && (cutsR[0] == 0);
		for (unsigned int c = 0; ok && (c < nc); ++c)
		{
			const unsigned int first = cutsR[c], last = (c + 1 < nc) ? cutsR[c + 1] : n;
			const unsigned int span = bounds[last] - bounds[first];
			ok = (last > first) && ((span <= cut) || (last == first + 1))
				&& ((last == n) || (bounds[last + 1] - bounds[first] > cut));
		}
		if (!ok)
		{
			if (firstBad < 0)
				firstBad = sample;
			++bad;
		}
	}
	EXPECT_EQ(0, bad) << bad << " of 20000 samples wrong, first at " << firstBad
		<< " (seed 0x" << std::hex << (unsigned int)BUDGETTEST_SEED << ")";
}

// [Kaori-Chani] The events command joins the last chunk's datagram only if the body, a 5-byte
// header and 5 more fit beside it: 1517 - 28 - 1000 = 489.
TEST(SnapshotBudget, TheEventsCommandJoinsTheLastDatagramOnlyIfItFits)
{
	EXPECT_TRUE(ComputeCosAppends(1000, 479, 1517, 28, 5));
	EXPECT_FALSE(ComputeCosAppends(1000, 480, 1517, 28, 5));
	EXPECT_FALSE(ComputeCosAppends(-1, 10, 1517, 28, 5));
	EXPECT_TRUE(ComputeCosAppends(0, 10, 1517, 28, 5));
}
