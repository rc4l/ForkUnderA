// [MGOOOOOO] Tests for the debug overlay's explosion-region store.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include "features/hitboxviz/computation/blastrecords_compute.h"

using namespace zx::hitboxviz;

namespace
{
	BlastRecord MakeRecord(float x, int expiryTic, int distance = 128, int fulldamage = 0)
	{
		BlastRecord record;
		record.x = x;
		record.y = 0.f;
		record.z = 0.f;
		record.distance = distance;
		record.fulldamagedistance = fulldamage;
		record.expiryTic = expiryTic;
		return record;
	}
}

// ---- basic storage ---------------------------------------------------------

TEST(HitboxVizBlastRecords, StartsEmpty)
{
	BlastRecordStore store;
	EXPECT_EQ(0u, store.Count());
}

TEST(HitboxVizBlastRecords, RoundTripsAllFields)
{
	BlastRecordStore store;

	BlastRecord record;
	record.x = 100.5f;
	record.y = -200.25f;
	record.z = 32.f;
	record.distance = 128;
	record.fulldamagedistance = 64;
	record.expiryTic = 500;
	store.Push(record);

	ASSERT_EQ(1u, store.Count());
	EXPECT_FLOAT_EQ(100.5f, store.Get(0).x);
	EXPECT_FLOAT_EQ(-200.25f, store.Get(0).y);
	EXPECT_FLOAT_EQ(32.f, store.Get(0).z);
	EXPECT_EQ(128, store.Get(0).distance);
	EXPECT_EQ(64, store.Get(0).fulldamagedistance);
	EXPECT_EQ(500, store.Get(0).expiryTic);
}

TEST(HitboxVizBlastRecords, IteratesOldestFirst)
{
	BlastRecordStore store;
	store.Push(MakeRecord(1.f, 100));
	store.Push(MakeRecord(2.f, 100));
	store.Push(MakeRecord(3.f, 100));

	ASSERT_EQ(3u, store.Count());
	EXPECT_FLOAT_EQ(1.f, store.Get(0).x);
	EXPECT_FLOAT_EQ(2.f, store.Get(1).x);
	EXPECT_FLOAT_EQ(3.f, store.Get(2).x);
}

// ---- expiry ----------------------------------------------------------------

TEST(HitboxVizBlastRecords, ExpiresAtTheBoundaryTic)
{
	BlastRecordStore store;
	store.Push(MakeRecord(1.f, /*expiryTic=*/100));

	// One tic before expiry: still drawn.
	store.ExpireBefore(99);
	EXPECT_EQ(1u, store.Count());

	// Exactly at the expiry tic: gone.
	store.ExpireBefore(100);
	EXPECT_EQ(0u, store.Count());
}

TEST(HitboxVizBlastRecords, ExpiresPastTheBoundaryTic)
{
	BlastRecordStore store;
	store.Push(MakeRecord(1.f, 100));

	store.ExpireBefore(1000);
	EXPECT_EQ(0u, store.Count());
}

TEST(HitboxVizBlastRecords, ExpiryKeepsSurvivorsInOrder)
{
	BlastRecordStore store;
	store.Push(MakeRecord(1.f, 10));
	store.Push(MakeRecord(2.f, 30));
	store.Push(MakeRecord(3.f, 20));
	store.Push(MakeRecord(4.f, 40));

	// Drops the two whose expiry has been reached (10 and 20), regardless of insertion order --
	// the store does not assume records were pushed in expiry order.
	store.ExpireBefore(25);

	ASSERT_EQ(2u, store.Count());
	EXPECT_FLOAT_EQ(2.f, store.Get(0).x);
	EXPECT_FLOAT_EQ(4.f, store.Get(1).x);
}

TEST(HitboxVizBlastRecords, ExpiryOnEmptyStoreIsHarmless)
{
	BlastRecordStore store;
	store.ExpireBefore(1000);
	EXPECT_EQ(0u, store.Count());
}

TEST(HitboxVizBlastRecords, ExpiryIsIdempotent)
{
	BlastRecordStore store;
	store.Push(MakeRecord(1.f, 10));
	store.Push(MakeRecord(2.f, 100));

	store.ExpireBefore(50);
	ASSERT_EQ(1u, store.Count());
	store.ExpireBefore(50);
	EXPECT_EQ(1u, store.Count());
	EXPECT_FLOAT_EQ(2.f, store.Get(0).x);
}

// ---- capacity --------------------------------------------------------------

TEST(HitboxVizBlastRecords, FillsToCapacityWithoutEviction)
{
	BlastRecordStore store;
	for (int i = 0; i < MAX_BLAST_RECORDS; ++i)
		store.Push(MakeRecord(static_cast<float>(i), 1000));

	ASSERT_EQ(static_cast<unsigned int>(MAX_BLAST_RECORDS), store.Count());
	EXPECT_FLOAT_EQ(0.f, store.Get(0).x);
	EXPECT_FLOAT_EQ(static_cast<float>(MAX_BLAST_RECORDS - 1), store.Get(MAX_BLAST_RECORDS - 1).x);
}

TEST(HitboxVizBlastRecords, EvictsOldestWhenFull)
{
	BlastRecordStore store;
	for (int i = 0; i < MAX_BLAST_RECORDS; ++i)
		store.Push(MakeRecord(static_cast<float>(i), 1000));

	// One past capacity: the oldest (0) is dropped, everything shifts down, the new one lands last.
	store.Push(MakeRecord(999.f, 1000));

	ASSERT_EQ(static_cast<unsigned int>(MAX_BLAST_RECORDS), store.Count());
	EXPECT_FLOAT_EQ(1.f, store.Get(0).x);
	EXPECT_FLOAT_EQ(static_cast<float>(MAX_BLAST_RECORDS - 1), store.Get(MAX_BLAST_RECORDS - 2).x);
	EXPECT_FLOAT_EQ(999.f, store.Get(MAX_BLAST_RECORDS - 1).x);
}

TEST(HitboxVizBlastRecords, SustainedOverflowNeverExceedsCapacity)
{
	BlastRecordStore store;
	for (int i = 0; i < MAX_BLAST_RECORDS * 3; ++i)
	{
		store.Push(MakeRecord(static_cast<float>(i), 1000));
		EXPECT_LE(store.Count(), static_cast<unsigned int>(MAX_BLAST_RECORDS));
	}

	ASSERT_EQ(static_cast<unsigned int>(MAX_BLAST_RECORDS), store.Count());
	// Only the most recent MAX_BLAST_RECORDS survive.
	EXPECT_FLOAT_EQ(static_cast<float>(MAX_BLAST_RECORDS * 3 - 1), store.Get(MAX_BLAST_RECORDS - 1).x);
	EXPECT_FLOAT_EQ(static_cast<float>(MAX_BLAST_RECORDS * 2), store.Get(0).x);
}

TEST(HitboxVizBlastRecords, ReusableAfterExpiringFromFull)
{
	BlastRecordStore store;
	for (int i = 0; i < MAX_BLAST_RECORDS; ++i)
		store.Push(MakeRecord(static_cast<float>(i), 100));

	store.ExpireBefore(100);
	ASSERT_EQ(0u, store.Count());

	store.Push(MakeRecord(7.f, 200));
	ASSERT_EQ(1u, store.Count());
	EXPECT_FLOAT_EQ(7.f, store.Get(0).x);
}

// ---- clear -----------------------------------------------------------------

TEST(HitboxVizBlastRecords, ClearDropsEverything)
{
	// Map change / reconnect / demo seek: stale regions would be drawn at coordinates belonging to
	// a different level.
	BlastRecordStore store;
	store.Push(MakeRecord(1.f, 1000));
	store.Push(MakeRecord(2.f, 1000));

	store.Clear();
	EXPECT_EQ(0u, store.Count());
}

TEST(HitboxVizBlastRecords, ClearOnEmptyStoreIsHarmless)
{
	BlastRecordStore store;
	store.Clear();
	EXPECT_EQ(0u, store.Count());
}

TEST(HitboxVizBlastRecords, LifetimeConstantIsAboutOneSecond)
{
	// 35 tics per second; the overlay is meant to linger just long enough to read.
	EXPECT_EQ(35, static_cast<int>(BLAST_RECORD_LIFETIME_TICS));
}
