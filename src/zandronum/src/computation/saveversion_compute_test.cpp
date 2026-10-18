// [rc4l] Tests for which savegame versions a build reads (saveversion_compute).
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "gtest/gtest.h"

#include "computation/saveversion_compute.h"
#include "version.h"

TEST(SaveVersion, ABuildReadsFromItsOldestToItsOwn)
{
	EXPECT_EQ(ComputeSaveVersionVerdict(4507, 4507, 4524), kSaveVersionReadable);
	EXPECT_EQ(ComputeSaveVersionVerdict(4524, 4507, 4524), kSaveVersionReadable);
	EXPECT_EQ(ComputeSaveVersionVerdict(4506, 4507, 4524), kSaveVersionTooOld);
	EXPECT_EQ(ComputeSaveVersionVerdict(0, 4507, 4524), kSaveVersionTooOld);
}

// [rc4l] An older build loaded a save holding features/decals' FlatDecal, stopped on the unknown class and stopped answering; a newer save is refused before any class is read.
TEST(SaveVersion, ASaveFromANewerBuildIsRefused)
{
	EXPECT_EQ(ComputeSaveVersionVerdict(4525, 4507, 4524), kSaveVersionTooNew);
	EXPECT_EQ(ComputeSaveVersionVerdict(SAVEVER + 1, MINSAVEVER, SAVEVER), kSaveVersionTooNew);
	EXPECT_EQ(ComputeSaveVersionVerdict(SAVEVER, MINSAVEVER, SAVEVER), kSaveVersionReadable);
}
