// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/frame/computation/ticstream_compute.h"

using namespace zx::frame;

namespace {
const TicStreamStamp kBuilt = { 40, 7, 0x1000, 0 };
}

// [Doruk-Aksoy] A live stream built from the same stamp is kept; none live is always built.
TEST(TicStreamStale, ALiveStreamWithTheSameStampIsKept)
{
	EXPECT_FALSE(ComputeTicStreamStale(true, kBuilt, kBuilt));
	EXPECT_TRUE(ComputeTicStreamStale(false, kBuilt, kBuilt));
}

// [Doruk-Aksoy] Each input alone makes it stale: a new tic, a setting changed while paused, another view actor, another fixed colormap.
TEST(TicStreamStale, AnyInputChangedBuildsItAgain)
{
	TicStreamStamp now = kBuilt;
	now.fill = 41;
	EXPECT_TRUE(ComputeTicStreamStale(true, kBuilt, now));
	now = kBuilt;
	now.cvarSets = 8;
	EXPECT_TRUE(ComputeTicStreamStale(true, kBuilt, now));
	now = kBuilt;
	now.viewActor = 0x2000;
	EXPECT_TRUE(ComputeTicStreamStale(true, kBuilt, now));
	now = kBuilt;
	now.fixedColormap = 3;
	EXPECT_TRUE(ComputeTicStreamStale(true, kBuilt, now));
}
