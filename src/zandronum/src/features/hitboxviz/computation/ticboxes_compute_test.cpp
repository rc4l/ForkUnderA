// [MGOOOOOO] Tests for the overlay's one-tic actors: which going actors are kept, and which frames show them.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "gtest/gtest.h"

#include "features/hitboxviz/computation/ticboxes_compute.h"

using namespace zx::hitboxviz;

namespace
{
	TicBoxRecord MakeBox(float x, int expiryTic)
	{
		TicBoxRecord box = {};
		box.x = x;
		box.radius = 32.f;
		box.height = 8.f;
		box.missile = true;
		box.expiryTic = expiryTic;
		return box;
	}
}

// ---- which actors -----------------------------------------------------------

TEST(HitboxVizTicBoxes, MissileGoneInItsSpawnTicIsKept)
{
	EXPECT_TRUE(ComputeTicBoxWanted(100, 100, true, false, false, false));
}

TEST(HitboxVizTicBoxes, SolidOrShootableGoneInItsSpawnTicIsKept)
{
	EXPECT_TRUE(ComputeTicBoxWanted(7, 7, false, true, false, false));
	EXPECT_TRUE(ComputeTicBoxWanted(7, 7, false, false, true, false));
}

// [MGOOOOOO] A script's actor spawned after its thinkers ran lives to the tic's end and dies next tic, already drawn by that tic's copy.
TEST(HitboxVizTicBoxes, ActorFromAnEarlierTicIsLeftToTheCopy)
{
	EXPECT_FALSE(ComputeTicBoxWanted(99, 100, true, true, true, false));
}

TEST(HitboxVizTicBoxes, ActorThatTouchesNothingIsSkipped)
{
	EXPECT_FALSE(ComputeTicBoxWanted(5, 5, false, false, false, false));
}

TEST(HitboxVizTicBoxes, NoInteractionIsSkippedWhateverItsFlags)
{
	EXPECT_FALSE(ComputeTicBoxWanted(5, 5, true, true, true, true));
}

// ---- which frames -----------------------------------------------------------

// [MGOOOOOO] Made at level time 40, it is shown by the copy at level time 41 (its tic's end, held through a pause) only, and dropped once a copy reaches 42.
TEST(HitboxVizTicBoxes, ShownByTheCopyAfterItsTicOnly)
{
	const int expiry = ComputeTicBoxExpiry(40);
	EXPECT_EQ(42, expiry);
	EXPECT_FALSE(ComputeTicBoxShown(expiry, 40));
	EXPECT_TRUE(ComputeTicBoxShown(expiry, 41));
	EXPECT_FALSE(ComputeTicBoxShown(expiry, 42));
}

// ---- the shared store with tic boxes -------------------------------------------

TEST(HitboxVizTicBoxes, StoreExpiresAPastTicKeepingTheShownOne)
{
	TicBoxStore store;
	store.Push(MakeBox(1.f, ComputeTicBoxExpiry(40)));
	store.Push(MakeBox(2.f, ComputeTicBoxExpiry(41)));

	store.ExpireBefore(42);

	ASSERT_EQ(1u, store.Count());
	EXPECT_FLOAT_EQ(2.f, store.Get(0).x);
	EXPECT_TRUE(ComputeTicBoxShown(store.Get(0).expiryTic, 42));
}

TEST(HitboxVizTicBoxes, StoreEvictsTheOldestWhenFull)
{
	TicBoxStore store;
	for (unsigned int i = 0; i < MAX_TIC_BOX_RECORDS + 3; ++i)
		store.Push(MakeBox(static_cast<float>(i), 10));

	ASSERT_EQ(static_cast<unsigned int>(MAX_TIC_BOX_RECORDS), store.Count());
	EXPECT_FLOAT_EQ(3.f, store.Get(0).x);
	EXPECT_FLOAT_EQ(static_cast<float>(MAX_TIC_BOX_RECORDS + 2), store.Get(MAX_TIC_BOX_RECORDS - 1).x);
}

TEST(HitboxVizTicBoxes, StoreRoundTripsEveryField)
{
	TicBoxRecord box = {};
	box.prevX = 1.f; box.prevY = 2.f; box.prevZ = 3.f;
	box.x = 4.f; box.y = 5.f; box.z = 6.f;
	box.radius = 32.f; box.height = 8.f;
	box.attackRadius = 40.f; box.attackHeight = 12.f;
	box.missile = true;
	box.expiryTic = 9;

	TicBoxStore store;
	store.Push(box);
	const TicBoxRecord &got = store.Get(0);
	EXPECT_FLOAT_EQ(1.f, got.prevX); EXPECT_FLOAT_EQ(2.f, got.prevY); EXPECT_FLOAT_EQ(3.f, got.prevZ);
	EXPECT_FLOAT_EQ(4.f, got.x); EXPECT_FLOAT_EQ(5.f, got.y); EXPECT_FLOAT_EQ(6.f, got.z);
	EXPECT_FLOAT_EQ(32.f, got.radius); EXPECT_FLOAT_EQ(8.f, got.height);
	EXPECT_FLOAT_EQ(40.f, got.attackRadius); EXPECT_FLOAT_EQ(12.f, got.attackHeight);
	EXPECT_TRUE(got.missile);
	EXPECT_EQ(9, got.expiryTic);

	store.Clear();
	EXPECT_EQ(0u, store.Count());
}
