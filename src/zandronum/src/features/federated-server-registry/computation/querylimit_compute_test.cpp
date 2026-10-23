// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/federated-server-registry/computation/querylimit_compute.h"

#include <gtest/gtest.h>

using namespace zx;

TEST(ComputeIgnoreLauncherQuery, EachPlayerBehindOneAddressIsAnswered)
{
	// [rc4l] The second of two clients on one address went unanswered for ten seconds, so its browser never listed the server.
	EXPECT_FALSE(ComputeIgnoreLauncherQuery(0, 4));
	EXPECT_FALSE(ComputeIgnoreLauncherQuery(1, 4));
	EXPECT_FALSE(ComputeIgnoreLauncherQuery(3, 4));
	EXPECT_TRUE(ComputeIgnoreLauncherQuery(4, 4));
	EXPECT_TRUE(ComputeIgnoreLauncherQuery(9, 4));
}

TEST(ComputeIgnoreLauncherQuery, OneIsTheLeastAnAddressGets)
{
	EXPECT_FALSE(ComputeIgnoreLauncherQuery(0, 0));
	EXPECT_TRUE(ComputeIgnoreLauncherQuery(1, 0));
	EXPECT_TRUE(ComputeIgnoreLauncherQuery(1, -3));
}
