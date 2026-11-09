// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Tests for the config resets keyed by Version-fua.

#include "features/config-migrate/computation/configmigrate_compute.h"

#include <gtest/gtest.h>

#include <cstring>

using namespace zx::configmigrate;

namespace
{
	const ConfigReset kTable[] =
	{
		{ 1, "a" },
		{ 3, "b" },
		{ 2, "c" },
		{ 3, "a" },
	};
	const size_t kCount = sizeof(kTable) / sizeof(kTable[0]);
}

TEST(ConfigMigrate, VersionIsTheTablesHighest)
{
	EXPECT_EQ(3, ComputeConfigVersion(kTable, kCount));
	EXPECT_EQ(0, ComputeConfigVersion(kTable, 0));
}

TEST(ConfigMigrate, LastVersionReadsOnlyAPlainNumber)
{
	EXPECT_EQ(0, ComputeConfigLastVersion(NULL));
	EXPECT_EQ(0, ComputeConfigLastVersion(""));
	EXPECT_EQ(0, ComputeConfigLastVersion("abc"));
	EXPECT_EQ(0, ComputeConfigLastVersion("2x"));
	EXPECT_EQ(0, ComputeConfigLastVersion("-4"));
	EXPECT_EQ(0, ComputeConfigLastVersion("99999999999"));
	EXPECT_EQ(0, ComputeConfigLastVersion("0"));
	EXPECT_EQ(7, ComputeConfigLastVersion("7"));
}

TEST(ConfigMigrate, ResetsAreTheRowsNewerThanTheConfigEachOnce)
{
	const std::vector<const char *> all = ComputeConfigResets(kTable, kCount, 0);
	ASSERT_EQ(3u, all.size());
	EXPECT_STREQ("a", all[0]);
	EXPECT_STREQ("b", all[1]);
	EXPECT_STREQ("c", all[2]);

	const std::vector<const char *> fromTwo = ComputeConfigResets(kTable, kCount, 2);
	ASSERT_EQ(2u, fromTwo.size());
	EXPECT_STREQ("b", fromTwo[0]);
	EXPECT_STREQ("a", fromTwo[1]);

	EXPECT_TRUE(ComputeConfigResets(kTable, kCount, 3).empty());
}

TEST(ConfigMigrate, ReplayGoesBackOffForConfigsSavedBeforeItsDefaultChanged)
{
	const int version = ComputeConfigVersion();
	EXPECT_GE(version, 1);

	const std::vector<const char *> old = ComputeConfigResets(0);
	bool replay = false;
	for (const char *name : old) replay = replay || strcmp(name, "cl_fua_replay") == 0;
	EXPECT_TRUE(replay);

	EXPECT_TRUE(ComputeConfigResets(version).empty());
}
