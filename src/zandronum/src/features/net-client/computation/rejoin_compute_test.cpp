// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/net-client/computation/rejoin_compute.h"

#include <gtest/gtest.h>

using namespace zx::netclient;

namespace {
const int kStartAsSpectator = 1 << 0;
const int kHideCountry = 1 << 2;
}

TEST(ComputeRejoinsAfterMapNew, OnlyAPlayerInTheGameRejoinsPlaying)
{
	EXPECT_TRUE(ComputeRejoinsAfterMapNew(true, false));
	EXPECT_FALSE(ComputeRejoinsAfterMapNew(true, true));
	EXPECT_FALSE(ComputeRejoinsAfterMapNew(false, false));
	EXPECT_FALSE(ComputeRejoinsAfterMapNew(false, true));
}

TEST(ComputeConnectFlags, ARejoinDropsStartAsSpectatorAndKeepsTheRest)
{
	EXPECT_EQ(ComputeConnectFlags(kStartAsSpectator | kHideCountry, kStartAsSpectator, true), kHideCountry);
	EXPECT_EQ(ComputeConnectFlags(kHideCountry, kStartAsSpectator, true), kHideCountry);
}

TEST(ComputeConnectFlags, AnyOtherAttemptSendsThePlayersFlags)
{
	EXPECT_EQ(ComputeConnectFlags(kStartAsSpectator | kHideCountry, kStartAsSpectator, false), kStartAsSpectator | kHideCountry);
	EXPECT_EQ(ComputeConnectFlags(0, kStartAsSpectator, false), 0);
}
