//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: anim_compute_test.cpp
//
// Description: Tests for anim_compute: the mirror spawn ring's re-entry, wrap
// and window, and the loop census's buckets and verdicts over chains of int
// states.
//
//-----------------------------------------------------------------------------

#include "anim_compute.h"
#include <gtest/gtest.h>

#include <string.h>

using namespace zx::netsnapshot;

// [Kaori-Chani] A state graph for the verdicts: 1 to 5 loop (5 back to 1), 10 to 13 end at 0,
// 20 to 30 a straight chain of eleven, and 40 has no successor.
static int animtest_Next(int s)
{
	if ((s >= 1) && (s <= 4))
		return s + 1;
	if (s == 5)
		return 1;
	if ((s >= 10) && (s <= 12))
		return s + 1;
	if ((s >= 20) && (s <= 29))
		return s + 1;
	return 0;
}

// [Kaori-Chani] Fills a ring with 22 states, state s entered at tic s + 10.
static void animtest_Fill(SnapMsRing<int>& ring)
{
	memset(&ring, 0, sizeof(ring));
	ComputeMsPush(ring, 1, 10);
	ComputeMsPush(ring, 2, 11);
	ComputeMsPush(ring, 2, 12);
	for (int s = 3; s <= 22; ++s)
		ComputeMsPush(ring, s, s + 10);
}

TEST(SnapshotAnim, AnEmptyMirrorSpawnRingNeverResyncs)
{
	SnapMsRing<int> ring;
	memset(&ring, 0, sizeof(ring));
	EXPECT_FALSE(ComputeMsResync(ring, 1, -1, 0));
}

// [Kaori-Chani] A state entered again keeps one entry, at its latest tic.
TEST(SnapshotAnim, MirrorSpawnRingKeepsOneEntryPerState)
{
	SnapMsRing<int> ring;
	memset(&ring, 0, sizeof(ring));
	ComputeMsPush(ring, 1, 10);
	ComputeMsPush(ring, 2, 11);
	ComputeMsPush(ring, 2, 12);
	EXPECT_EQ(2u, ring.count);
	EXPECT_EQ(2, ring.e[ring.head].state);
	EXPECT_EQ(12, ring.e[ring.head].tic);
}

// [Kaori-Chani] 22 states in all: the ring keeps the newest 16, states 7 to 22.
TEST(SnapshotAnim, MirrorSpawnRingWrapsToTheNewestEntries)
{
	SnapMsRing<int> ring;
	animtest_Fill(ring);
	EXPECT_EQ((unsigned int)SNAP_MS_RING, ring.count);
	EXPECT_EQ(22, ring.e[ring.head].state);
	EXPECT_EQ(7, ComputeMsOldest(ring).state);
	EXPECT_EQ(17, ComputeMsOldest(ring).tic);
}

// [Kaori-Chani] State 22 was entered at tic 32, the earliest a 4-tic state read at 40 may have
// been (40 - 4 - SNAP_MS_SLACK), so a 3-tic read is already too late.
TEST(SnapshotAnim, MirrorSpawnRingResyncsOnlyInsideItsWindow)
{
	SnapMsRing<int> ring;
	animtest_Fill(ring);
	EXPECT_TRUE(ComputeMsResync(ring, 22, 4, 40));
	EXPECT_FALSE(ComputeMsResync(ring, 22, 3, 40));
}

// [Kaori-Chani] An endless state resyncs however late it is read, but only if the ring still holds
// it.
TEST(SnapshotAnim, MirrorSpawnRingResyncsAnEndlessStateAtAnyAge)
{
	SnapMsRing<int> ring;
	animtest_Fill(ring);
	EXPECT_TRUE(ComputeMsResync(ring, 7, -1, 1000));
	EXPECT_FALSE(ComputeMsResync(ring, 6, -1, 1000));
}

// [Kaori-Chani] Each bucket edge's tics either side, and a negative gap, which lands in the first.
TEST(SnapshotAnim, ShadowBucketTakesItsEdges)
{
	static const struct { int gap; unsigned int want; } buckets[] =
	{
		{ -1, 0 }, { 0, 0 }, { 3, 0 }, { 4, 1 }, { 8, 1 }, { 9, 2 },
		{ 17, 2 }, { 18, 3 }, { 35, 3 }, { 36, 4 }, { 1000, 4 },
	};
	for (unsigned int i = 0; i < sizeof(buckets) / sizeof(buckets[0]); ++i)
	{
		EXPECT_EQ(buckets[i].want, ComputeShadowBucket(buckets[i].gap))
			<< "bucket at " << buckets[i].gap << " tics";
	}
}

TEST(SnapshotAnim, ShadowVerdictWalksTheStateGraph)
{
	static const struct { int shadow, wire, want; } cases[] =
	{
		{ 3, 3, SNAP_SHADOW_EXACT },
		{ 2, 4, SNAP_SHADOW_BEHIND },		// forward on the shadow's chain
		{ 20, 28, SNAP_SHADOW_BEHIND },		// 8 steps: the walk's last
		{ 20, 29, SNAP_SHADOW_OFF },		// 9 steps: past it, either way
		{ 28, 20, SNAP_SHADOW_AHEAD },		// the wire's chain reaches the shadow
		{ 4, 2, SNAP_SHADOW_BEHIND },		// a loop reaches both ways; behind is tried first
		{ 10, 3, SNAP_SHADOW_OFF },			// the shadow's chain ends at 0
		{ 40, 10, SNAP_SHADOW_OFF },		// no successor at all
		{ 13, 11, SNAP_SHADOW_AHEAD },		// from the end of a chain, only ahead
	};
	for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		EXPECT_EQ(cases[i].want, ComputeShadowVerdict(cases[i].shadow, cases[i].wire, animtest_Next))
			<< "case " << i << ": shadow " << cases[i].shadow << ", wire " << cases[i].wire;
	}
}
