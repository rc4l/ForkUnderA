// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/keptobjects_compute.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

using zx::hwrender::ComputeBufferOwnsMemory;
using zx::hwrender::KeptObjects;

namespace {

// [rc4l] An owner the test lets go of, as the backend lets go of a texture when the page is resized.
using Owner = std::weak_ptr<int>;

struct Freed { void *handle; bool texture; };

bool Released(const Owner &o) { return o.expired(); }

// [rc4l] How many objects kept still holds, counted by freeing a copy of it as if nothing held any.
unsigned Held(KeptObjects<Owner> kept) { return kept.FreeReleased([](const Owner &) { return true; }, [](void *, bool) {}); }

} // namespace

TEST(KeptObjects, OnlyTheReleasedAreFreed)
{
	int a = 0, b = 0;
	auto holdA = std::make_shared<int>(1), holdB = std::make_shared<int>(2);
	KeptObjects<Owner> kept;
	ASSERT_TRUE(kept.Keep(&a, true, holdA));
	ASSERT_TRUE(kept.Keep(&b, false, holdB));
	std::vector<Freed> freed;
	const auto free = [&](void *h, bool texture) { freed.push_back({ h, texture }); };

	EXPECT_EQ(0u, kept.FreeReleased(Released, free));
	EXPECT_TRUE(freed.empty());

	holdA.reset();
	EXPECT_EQ(1u, kept.FreeReleased(Released, free));
	ASSERT_EQ(1u, freed.size());
	EXPECT_EQ(&a, freed[0].handle);
	EXPECT_TRUE(freed[0].texture);
	EXPECT_EQ(1u, Held(kept));

	holdB.reset();
	EXPECT_EQ(1u, kept.FreeReleased(Released, free));
	ASSERT_EQ(2u, freed.size());
	EXPECT_EQ(&b, freed[1].handle);
	EXPECT_FALSE(freed[1].texture);
	EXPECT_EQ(0u, Held(kept));
}

// [rc4l] The swapchain's own textures are offered after every present, and kept once until they go.
TEST(KeptObjects, AHandleIsKeptOnce)
{
	int a = 0;
	auto hold = std::make_shared<int>(1);
	KeptObjects<Owner> kept;
	EXPECT_TRUE(kept.Keep(&a, true, hold));
	EXPECT_FALSE(kept.Keep(&a, true, hold));
	EXPECT_EQ(1u, Held(kept));
}

// [rc4l] A browser numbers its textures and its buffers apart, so a buffer can share a kept texture's number; it went unkept and waited on the garbage collector, five level vertex buffers at once on Sunder (queue #48).
TEST(KeptObjects, ATextureAndABufferWithOneHandleAreKeptApart)
{
	int a = 0;
	auto texture = std::make_shared<int>(1), buffer = std::make_shared<int>(2);
	KeptObjects<Owner> kept;
	EXPECT_TRUE(kept.Keep(&a, true, texture));
	EXPECT_TRUE(kept.Keep(&a, false, buffer));
	EXPECT_FALSE(kept.Keep(&a, false, buffer));
	EXPECT_EQ(2u, Held(kept));
	std::vector<Freed> freed;
	buffer.reset();
	EXPECT_EQ(1u, kept.FreeReleased(Released, [&](void *h, bool t) { freed.push_back({ h, t }); }));
	ASSERT_EQ(1u, freed.size());
	EXPECT_FALSE(freed[0].texture);
	auto again = std::make_shared<int>(3);
	EXPECT_TRUE(kept.Keep(&a, false, again));
	EXPECT_FALSE(kept.Keep(&a, true, texture));
}

TEST(KeptObjects, NoHandleIsNothingToKeep)
{
	KeptObjects<Owner> kept;
	EXPECT_FALSE(kept.Keep(nullptr, true, Owner()));
	EXPECT_EQ(0u, Held(kept));
}

// [rc4l] Freed handles are forgotten, so an object made later at the same address is kept again.
TEST(KeptObjects, AFreedHandleCanBeKeptAgain)
{
	int a = 0;
	KeptObjects<Owner> kept;
	{
		auto hold = std::make_shared<int>(1);
		ASSERT_TRUE(kept.Keep(&a, true, hold));
	}
	EXPECT_EQ(1u, kept.FreeReleased(Released, [](void *, bool) {}));
	auto again = std::make_shared<int>(2);
	EXPECT_TRUE(kept.Keep(&a, true, again));
	EXPECT_EQ(0u, kept.FreeReleased(Released, [](void *, bool) {}));
}

// [rc4l] Freeing from the middle leaves every other kept object in place.
TEST(KeptObjects, FreeingSeveralAtOnceKeepsTheRest)
{
	int h[4] = {};
	std::vector<std::shared_ptr<int>> holds;
	KeptObjects<Owner> kept;
	for (int i = 0; i < 4; i++)
	{
		holds.push_back(std::make_shared<int>(i));
		kept.Keep(&h[i], i % 2 == 0, holds.back());
	}
	holds[0].reset();
	holds[2].reset();
	std::vector<void *> freed;
	EXPECT_EQ(2u, kept.FreeReleased(Released, [&](void *p, bool) { freed.push_back(p); }));
	EXPECT_EQ(2u, Held(kept));
	EXPECT_NE(freed.end(), std::find(freed.begin(), freed.end(), (void *)&h[0]));
	EXPECT_NE(freed.end(), std::find(freed.begin(), freed.end(), (void *)&h[2]));
	holds[1].reset();
	holds[3].reset();
	EXPECT_EQ(2u, kept.FreeReleased(Released, [](void *, bool) {}));
	EXPECT_EQ(0u, Held(kept));
}

TEST(KeptObjects, OnlyABufferWithItsOwnMemoryIsKept)
{
	EXPECT_TRUE(ComputeBufferOwnsMemory(false, false));
	EXPECT_FALSE(ComputeBufferOwnsMemory(true, false));
	EXPECT_FALSE(ComputeBufferOwnsMemory(false, true));
}
