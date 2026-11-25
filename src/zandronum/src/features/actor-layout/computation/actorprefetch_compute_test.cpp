// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/actor-layout/computation/actorprefetch_compute.h"
#include <gtest/gtest.h>
#include <cstddef>

// [Doruk-Aksoy] A server also fetches the position-update block; a client or a single player stops at the hot groups.
TEST(ComputeTickPrefetchBytes, ServerTakesTheUpdateBlock)
{
	EXPECT_EQ(ComputeTickPrefetchBytes(true, 640, 720), (size_t)720);
	EXPECT_EQ(ComputeTickPrefetchBytes(false, 640, 720), (size_t)640);
}
