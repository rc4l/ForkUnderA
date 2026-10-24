// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <unordered_set>

#include "features/hwrender/computation/bindingkey_compute.h"

using namespace zx::hwrender;

namespace {

const void *Obj(int n) { return reinterpret_cast<const void *>((size_t)(n + 1) * 256); }

}

// [rc4l] The deferred decal pass re-pointed one binding per run of marks, about 13,000 new WebGPU bind groups a second in a Complex Doom fight, and Firefox ran out of memory.
TEST(ComputeBindingKey, KeysDifferInAnyMemberAndMatchOnAll)
{
	const BindingKey k = { Obj(1), Obj(2), Obj(3), 0 };
	EXPECT_TRUE(k == (BindingKey{ Obj(1), Obj(2), Obj(3), 0 }));
	EXPECT_FALSE(k == (BindingKey{ Obj(9), Obj(2), Obj(3), 0 }));
	EXPECT_FALSE(k == (BindingKey{ Obj(1), Obj(9), Obj(3), 0 }));
	EXPECT_FALSE(k == (BindingKey{ Obj(1), Obj(2), Obj(9), 0 }));
	EXPECT_FALSE(k == (BindingKey{ Obj(1), Obj(2), Obj(3), 1 }));
	EXPECT_EQ(ComputeBindingKeyHash(k), BindingKeyHash()(BindingKey{ Obj(1), Obj(2), Obj(3), 0 }));
}

TEST(ComputeBindingKey, HashesSpreadAcrossGraphicsAndPasses)
{
	std::unordered_set<size_t> seen;
	for (int tex = 0; tex < 300; tex++)
		for (int pass = 0; pass < 3; pass++)
			seen.insert(ComputeBindingKeyHash(BindingKey{ Obj(tex), Obj(1000), Obj(1001), pass }));
	EXPECT_EQ(900u, seen.size());
}

TEST(ComputeBindingCacheFull, EmptiedOnceItReachesItsLimit)
{
	EXPECT_FALSE(ComputeBindingCacheFull(0, 1024));
	EXPECT_FALSE(ComputeBindingCacheFull(1023, 1024));
	EXPECT_TRUE(ComputeBindingCacheFull(1024, 1024));
	EXPECT_TRUE(ComputeBindingCacheFull(5000, 1024));
}
