// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: ack_compute_test.cpp
//
// Description: Tests for ack_compute: an ack's standing, every verdict on a
// pending datagram, a stream of datagrams lost, late and reordered read back
// through its acks, and the ack lag and the client's settled test over a
// lossy stream.
//
//-----------------------------------------------------------------------------

#include "ack_compute.h"
#include <gtest/gtest.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] The history a client sends with an ack of `seq`, from what it has received.
static unsigned int acktest_History(const bool* received, unsigned int seq)
{
	unsigned int history = 0;
	for (unsigned int back = 1; (back <= SNAP_ACK_HISTORY) && (back < seq); ++back)
	{
		if (received[seq - back])
			history |= (1u << (back - 1));
	}
	return history;
}

// [Kaori-Chani] Whether an ack is read, and whether its history can be.
TEST(SnapshotAck, AnAckIsReadOnlyWhenItIsNewAndAlreadySent)
{
	static const struct
	{
		bool bHave; unsigned int last, seq, next; bool bStale, bWide, bBits, bAhead;
	} cases[] =
	{
		{ false, 0, 7, 100, false, false, false, false },	// the first: only its own
		{ false, 50, 7, 100, false, false, false, false },	// nothing to be older than
		{ true, 10, 9, 100, true, false, false, false },	// older: ignored
		{ true, 10, 10, 100, false, false, true, false },	// the same: a late bit
		{ true, 10, 11, 100, false, false, true, false },
		{ true, 10, 10 + SNAP_ACK_HISTORY, 100, false, false, true, false },
		{ true, 10, 11 + SNAP_ACK_HISTORY, 100, false, true, false, false },
		{ true, 10, 19, 20, false, false, true, false },	// the newest sent
		{ true, 10, 20, 20, false, false, false, true },	// not yet sent: ignored
		{ true, 10, 90000, 20, false, false, false, true },	// another stream's
		{ false, 0, 7, 7, false, false, false, true },		// the first, unsent
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		const SnapAckRead r = ComputeAckRead(cases[i].bHave, cases[i].last, cases[i].seq,
			cases[i].next);
		EXPECT_EQ(cases[i].bStale, r.bStale) << "case " << i << ": stale";
		EXPECT_EQ(cases[i].bWide, r.bWide) << "case " << i << ": wide";
		EXPECT_EQ(cases[i].bBits, r.bCanReadBits) << "case " << i << ": bits";
		EXPECT_EQ(cases[i].bAhead, r.bAhead) << "case " << i << ": ahead";
	}
}

// [Kaori-Chani] Each verdict, at the edges of the history and the reorder window, for an ack of
// 100.
TEST(SnapshotAck, EachVerdictAtTheEdgesOfTheHistoryAndWindow)
{
	static const struct
	{
		unsigned int history; bool bBits; unsigned int pf; int want;
	} cases[] =
	{
		{ 0, true, 101, SNAP_ACK_AHEAD },
		{ 0, true, 100, SNAP_ACK_DELIVERED },			// the acked one, bits or not
		{ 0, false, 100, SNAP_ACK_DELIVERED },
		{ 1u << 0, true, 99, SNAP_ACK_DELIVERED },		// its bit, one back
		{ 1u << 31, true, 68, SNAP_ACK_DELIVERED },		// and the last bit
		{ 0xFFFFFFFEu, true, 99, SNAP_ACK_WAIT },		// the others' bits are not its own
		{ 0, true, 100 - SNAP_REORDER_WINDOW + 1, SNAP_ACK_WAIT },
		{ 0, true, 100 - SNAP_REORDER_WINDOW, SNAP_ACK_LOST },
		{ 0x7FFFFFFFu, true, 68, SNAP_ACK_LOST },
		{ 0, true, 99 - SNAP_ACK_HISTORY, SNAP_ACK_LOST_UNPROVEN },	// past the history
		{ 0xFFFFFFFFu, false, 99, SNAP_ACK_LOST_UNPROVEN },	// unreadable bits prove nothing
		{ 0, false, 99, SNAP_ACK_LOST_UNPROVEN },			// nor wait
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want,
			ComputeAckVerdict(100, cases[i].history, cases[i].bBits, cases[i].pf))
			<< "case " << i;
	}
}

// [Kaori-Chani] Beyond the history, a set bit is never read, nor for a datagram after the ack
// however far the unsigned distance back wraps.
TEST(SnapshotAck, TheHistorysReachIsBounded)
{
	const unsigned int allSet = 0xFFFFFFFFu;
	EXPECT_FALSE(ComputeProvenDelivered(100, allSet, true, 99 - SNAP_ACK_HISTORY));
	EXPECT_FALSE(ComputeProvenDelivered(100, allSet, true, 101));
	EXPECT_FALSE(ComputeProvenDelivered(5, allSet, true, 0xFFFFFFFFu));
	EXPECT_TRUE(ComputeProvenDelivered(100, allSet, true, 100 - SNAP_ACK_HISTORY));
}

// [Kaori-Chani] Datagrams 1-40, 5 and 20 dropped, 10 after 13 and 30 after 39 (past the window),
// each acked and walked as OnAck does.
TEST(SnapshotAck, AStreamOfLostAndReorderedDatagramsResolvesEachOne)
{
	static const unsigned int arrivals[] =
	{
		1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 10, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24,
		25, 26, 27, 28, 29, 31, 32, 33, 34, 35, 36, 37, 38, 39, 30, 40,
	};
	bool received[41] = {};
	int verdict[41] = {};		// 0 while pending, then the resolving verdict
	bool sawWait[41] = {};
	unsigned int head = 1;		// the oldest pending datagram
	bool bHave = false;
	unsigned int last = 0, newest = 0;
	for (unsigned int a = 0; a < sizeof(arrivals) / sizeof(arrivals[0]); ++a)
	{
		received[arrivals[a]] = true;
		if (arrivals[a] > newest)
			newest = arrivals[a];
		const unsigned int history = acktest_History(received, newest);
		const SnapAckRead r = ComputeAckRead(bHave, last, newest, 41);
		if (r.bStale)
			continue;
		for (; head <= 40; ++head)
		{
			const int v = ComputeAckVerdict(newest, history, r.bCanReadBits, head);
			if (v == SNAP_ACK_AHEAD)
				break;
			if (v == SNAP_ACK_WAIT)
			{
				sawWait[head] = true;
				break;
			}
			verdict[head] = v;
		}
		bHave = true;
		last = newest;
	}

	for (unsigned int s = 1; s <= 40; ++s)
	{
		const bool bLost = (s == 5) || (s == 20) || (s == 30);
		const bool bWaited = (s == 5) || (s == 10) || (s == 20) || (s == 30);
		EXPECT_EQ(bLost ? SNAP_ACK_LOST : SNAP_ACK_DELIVERED, verdict[s]) << "datagram " << s;
		EXPECT_EQ(bWaited, sawWait[s]) << "datagram " << s << " waited";
	}
}

// [Kaori-Chani] An ack after a silence wider than the history proves nothing older.
TEST(SnapshotAck, AnAckAfterAWideSilenceProvesNothingOlder)
{
	const SnapAckRead r = ComputeAckRead(true, 40, 41 + SNAP_ACK_HISTORY, 42 + SNAP_ACK_HISTORY);
	EXPECT_TRUE(r.bWide);
	EXPECT_EQ(SNAP_ACK_LOST_UNPROVEN, ComputeAckVerdict(41 + SNAP_ACK_HISTORY, 0xFFFFFFFFu,
		r.bCanReadBits, 40 + SNAP_ACK_HISTORY));
}

TEST(SnapshotAck, TheAckLagAtItsEdges)
{
	EXPECT_EQ(SNAP_ACK_LAG_NONE, ComputeAckLag(false, 50, 60));
	EXPECT_EQ(1u, ComputeAckLag(true, 50, 51));
	EXPECT_EQ(254u, ComputeAckLag(true, 50, 304));
	EXPECT_EQ(SNAP_ACK_LAG_FAR, ComputeAckLag(true, 50, 305));
	EXPECT_EQ(SNAP_ACK_LAG_FAR, ComputeAckLag(true, 50, 5000));
	EXPECT_EQ(1u, ComputeAckLag(true, 50, 50));
	EXPECT_EQ(SNAP_ACK_LAG_FAR, ComputeAckLag(true, 50, 40)) << "an ack ahead reads far";
}

TEST(SnapshotAck, TheSettledTestAtItsEdges)
{
	static const struct { unsigned int seq, lag, back; bool bWant; } cases[] =
	{
		{ 100, 1, 9, true },	// the echoed ack is 8 past the loss
		{ 100, 1, 8, false },
		{ 100, 2, 9, false },
		{ 100, 24, 32, true },
		{ 100, 25, 32, false },
		{ 100, SNAP_ACK_LAG_NONE, 20, false },	// no ack processed: nothing repaired
		{ 100, SNAP_ACK_LAG_FAR, 32, false },
		{ 100, 25, 0, true },	// none within the history: one older needs E >= seq - 25
		{ 100, 26, 0, false },
		{ 100, SNAP_ACK_LAG_NONE, 0, false },
		{ 33, SNAP_ACK_LAG_NONE, 0, true },		// nothing older than the history exists
		{ 34, SNAP_ACK_LAG_NONE, 0, false },
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].bWant, ComputeAckSettled(cases[i].seq, cases[i].lag, cases[i].back))
			<< "case " << i;
	}
}

// [Kaori-Chani] Three datagrams a frame, one in 41 and one in 97 dropped, a frame each way, acks
// read before each build: all the client settles was built after every repair.
TEST(SnapshotAck, NothingSettlesBeforeItsRepairOverALossyStream)
{
	enum { FRAMES = 400, PER_FRAME = 3, N = FRAMES * PER_FRAME };
	static bool received[N + 1], lost[N + 1];
	static int resolvedAt[N + 1], builtAt[N + 1];
	static unsigned int lagOf[N + 1];
	struct Ack { int due; unsigned int seq, history; };
	static Ack acks[N + 1];
	unsigned int nAcks = 0, nextAck = 0;
	unsigned int head = 1, nextSeq = 1;		// the server's oldest pending, and next to send
	bool bHave = false;
	unsigned int last = 0, newest = 0;
	unsigned int unsafe = 0, settledAfterLoss = 0;
	for (unsigned int s = 0; s <= N; ++s)
	{
		received[s] = lost[s] = false;
		resolvedAt[s] = -1;
	}
	for (int frame = 0; frame < FRAMES + 4; ++frame)
	{
		// The server reads the acks that have arrived, then builds.
		for (; (nextAck < nAcks) && (acks[nextAck].due <= frame); ++nextAck)
		{
			const Ack& a = acks[nextAck];
			const SnapAckRead r = ComputeAckRead(bHave, last, a.seq, nextSeq);
			if (r.bStale)
				continue;
			for (; head < nextSeq; ++head)
			{
				const int v = ComputeAckVerdict(a.seq, a.history, r.bCanReadBits, head);
				if ((v == SNAP_ACK_AHEAD) || (v == SNAP_ACK_WAIT))
					break;
				resolvedAt[head] = frame;
			}
			bHave = true;
			last = a.seq;
		}
		if (frame >= FRAMES)
			continue;
		for (unsigned int k = 0; k < PER_FRAME; ++k, ++nextSeq)
		{
			builtAt[nextSeq] = frame;
			lagOf[nextSeq] = ComputeAckLag(bHave, last, nextSeq);
			lost[nextSeq] = ((nextSeq % 41u) == 17u) || ((nextSeq % 97u) == 18u);
		}
		// What arrives this frame is what was sent last frame; each arrival is checked against
		// the client's history, then acked.
		if (frame == 0)
			continue;
		for (unsigned int k = 0; k < PER_FRAME; ++k)
		{
			const unsigned int d = (unsigned int)(frame - 1) * PER_FRAME + 1 + k;
			if (lost[d])
				continue;
			unsigned int back = 0;
			for (unsigned int b = 1; (b <= SNAP_ACK_HISTORY) && (b < d); ++b)
			{
				if (received[d - b] == false)
				{
					back = b;
					break;
				}
			}
			if (ComputeAckSettled(d, lagOf[d], back))
			{
				bool bLossBehind = false;
				for (unsigned int l = 1; l < d; ++l)
				{
					if (lost[l] && ((resolvedAt[l] < 0) || (resolvedAt[l] > builtAt[d])))
						++unsafe;
					if (lost[l] && (d - l <= SNAP_ACK_HISTORY))
						bLossBehind = true;
				}
				if (bLossBehind)
					++settledAfterLoss;
			}
			received[d] = true;
			if (d > newest)
				newest = d;
			acks[nAcks].due = frame + 1;
			acks[nAcks].seq = newest;
			acks[nAcks].history = acktest_History(received, newest);
			++nAcks;
		}
	}
	EXPECT_EQ(0u, unsafe) << "datagrams settled before a repair";
	EXPECT_GT(settledAfterLoss, 0u) << "the run must exercise settling with a loss behind";
}

TEST(SnapshotAck, TheTooLateTestAtItsEdges)
{
	EXPECT_FALSE(ComputeAckTooLate(100, 100, 0));
	EXPECT_FALSE(ComputeAckTooLate(101, 100, 0));
	EXPECT_FALSE(ComputeAckTooLate(93, 100, 0)) << "7 back: may be late";
	EXPECT_TRUE(ComputeAckTooLate(92, 100, 0)) << "8 back, missing";
	EXPECT_FALSE(ComputeAckTooLate(92, 100, 1u << 7)) << "a duplicate";
	EXPECT_TRUE(ComputeAckTooLate(68, 100, 0x7FFFFFFFu));
	EXPECT_TRUE(ComputeAckTooLate(67, 100, 0)) << "past the history";
	EXPECT_FALSE(ComputeAckTooLate(5000, 0, 0)) << "the first after a join";
}

// [Kaori-Chani] The fold: forward, past the mask, a reordered bit, a duplicate, and one at the
// mask's edge, which sets nothing.
TEST(SnapshotAck, TheFoldWalksTheHistoryForward)
{
	unsigned int last = 100, hist = 0x1u;
	ComputeAckFold(103, last, hist);
	EXPECT_EQ(103u, last);
	EXPECT_EQ(0xCu, hist);
	ComputeAckFold(101, last, hist);
	EXPECT_EQ(103u, last);
	EXPECT_EQ(0xEu, hist);
	ComputeAckFold(103, last, hist);
	EXPECT_EQ(103u, last);
	EXPECT_EQ(0xEu, hist);
	ComputeAckFold(71, last, hist);
	EXPECT_EQ(0xEu, hist);
	ComputeAckFold(140, last, hist);
	EXPECT_EQ(140u, last);
	EXPECT_EQ(0u, hist);
}

TEST(SnapshotAck, TheLostMaskAdvancesAndMarks)
{
	EXPECT_EQ(0u, ComputeLostMaskAdvance(0xFu, false, 10, 20));
	EXPECT_EQ(0x1u, ComputeLostMaskAdvance(0x1u, true, 10, 10));
	EXPECT_EQ(0x8u, ComputeLostMaskAdvance(0x1u, true, 10, 13));
	EXPECT_EQ(0x80000000u, ComputeLostMaskAdvance(0x1u, true, 10, 41));
	EXPECT_EQ(0u, ComputeLostMaskAdvance(0x1u, true, 10, 42));
	EXPECT_EQ(0u, ComputeLostMaskAdvance(0x1u, true, 10, 9));

	EXPECT_EQ(0u, ComputeLostMaskMark(0, 50, 50));
	EXPECT_EQ(1u, ComputeLostMaskMark(0, 50, 49));
	EXPECT_EQ(0x80000000u, ComputeLostMaskMark(0, 50, 18));
	EXPECT_EQ(4u, ComputeLostMaskMark(4u, 50, 17));
}

TEST(SnapshotAck, BitsSetAfterADeclaredLossAreCounted)
{
	EXPECT_EQ(3u, ComputeSetAfterLost(0x8000000Fu, 0x80000005u));
	EXPECT_EQ(0u, ComputeSetAfterLost(0xFu, 0xF0u));
}

// [Kaori-Chani] A stream holding one datagram in seven back by 12 and by 40, dropped as too late
// or applied anyway; past the history a held one sets no bit when it lands.
TEST(SnapshotAck, AHeldDatagramIsEitherDroppedOrLandsWithoutSettingABit)
{
	for (int pass = 0; pass < 4; ++pass)
	{
		const bool bDrop = ((pass % 2) == 0);
		enum { N = 600, MAXHOLD = 40 };
		const unsigned int HOLD = (pass < 2) ? 12 : MAXHOLD;
		static bool received[N + MAXHOLD + 1], declaredLost[N + MAXHOLD + 1];
		for (unsigned int s = 0; s <= N + HOLD; ++s)
			received[s] = declaredLost[s] = false;
		unsigned int clientLast = 0, clientHistory = 0;
		unsigned int head = 1, last = 0, lostMask = 0, setAfter = 0, dropped = 0, heldCount = 0;
		bool bHave = false;
		// Datagram s leaves at slot s, or s + HOLD if held; each arrival is acked at once and
		// read by the server at once.
		for (unsigned int slot = 1; slot <= N + HOLD; ++slot)
		{
			for (int k = 0; k < 2; ++k)
			{
				const unsigned int s = (k == 0) ? slot : slot - HOLD;
				if ((s < 1) || (s > N))
					continue;
				const bool bHeld = ((s % 7) == 3) && ((s + HOLD) <= N);
				if (bHeld != (k == 1))
					continue;
				heldCount += bHeld ? 1 : 0;
				if (ComputeAckTooLate(s, clientLast, clientHistory))
				{
					++dropped;
					if (bDrop)
						continue;
				}
				received[s] = true;
				ComputeAckFold(s, clientLast, clientHistory);

				const SnapAckRead r = ComputeAckRead(bHave, last, clientLast, N + 1);
				if (r.bStale)
					continue;
				lostMask = ComputeLostMaskAdvance(lostMask, bHave, last, clientLast);
				setAfter += ComputeSetAfterLost(lostMask, clientHistory);
				lostMask &= ~clientHistory;
				for (; head <= N; ++head)
				{
					const int v = ComputeAckVerdict(clientLast, clientHistory, r.bCanReadBits, head);
					if ((v == SNAP_ACK_AHEAD) || (v == SNAP_ACK_WAIT))
						break;
					if (v != SNAP_ACK_DELIVERED)
					{
						declaredLost[head] = true;
						lostMask = ComputeLostMaskMark(lostMask, clientLast, head);
					}
				}
				bHave = true;
				last = clientLast;
			}
		}
		unsigned int heldLost = 0, heldHeld = 0;
		for (unsigned int s = 1; s <= N; ++s)
		{
			if (((s % 7) == 3) && ((s + HOLD) <= N))
			{
				heldLost += declaredLost[s] ? 1 : 0;
				heldHeld += (declaredLost[s] && received[s]) ? 1 : 0;
			}
		}
		const char* const what = bDrop ? "dropped" : "applied";
		if (bDrop)
		{
			EXPECT_EQ(0u, setAfter) << what << " by " << HOLD;
			EXPECT_EQ(0u, heldHeld) << what << " by " << HOLD;
			EXPECT_EQ(heldCount, dropped) << what << " by " << HOLD;
			EXPECT_EQ(heldCount, heldLost) << what << " by " << HOLD;
		}
		else
		{
			EXPECT_EQ((HOLD <= SNAP_ACK_HISTORY) ? heldCount : 0u, setAfter)
				<< what << " by " << HOLD;
			EXPECT_EQ(heldCount, heldHeld) << what << " by " << HOLD;
		}
	}
}
