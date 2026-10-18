// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/lock_compute.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

std::string g_taking, g_held;
int g_nested = 0;

void Nested(const char *taking, const char *held)
{
	g_nested++;
	g_taking = taking;
	g_held = held;
}

} // namespace

TEST(Lock, ATakeIsCountedAndHeldUntilReleased)
{
	zx::Lock lock("names");
	EXPECT_STREQ("names", lock.Name());
	EXPECT_EQ(nullptr, zx::HeldLock());
	{
		zx::LockScope hold(lock);
		EXPECT_EQ(&lock, zx::HeldLock());
	}
	EXPECT_EQ(nullptr, zx::HeldLock());
	EXPECT_EQ(1u, lock.Takes());
	EXPECT_EQ(0u, lock.Waits());
}

TEST(Lock, TakingASecondLockWhileHoldingOneIsReported)
{
	zx::SetLockNestedListener(Nested);
	g_nested = 0;
	zx::Lock outer("textures"), inner("names");
	{
		zx::LockScope a(outer);
		zx::LockScope b(inner);
		EXPECT_EQ(&outer, zx::HeldLock());
	}
	EXPECT_EQ(nullptr, zx::HeldLock());
	EXPECT_EQ(1, g_nested);
	EXPECT_EQ("names", g_taking);
	EXPECT_EQ("textures", g_held);
	zx::SetLockNestedListener(nullptr);
	{
		zx::LockScope a(outer);
		zx::LockScope b(inner);
	}
	EXPECT_EQ(1, g_nested);
}

TEST(Lock, ATakeThatFindsItHeldByAnotherThreadCountsAWait)
{
	zx::Lock lock("colormaps");
	std::atomic<bool> holding(false), release(false);
	std::thread other([&] {
		zx::LockScope hold(lock);
		holding = true;
		while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	});
	while (!holding) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	std::thread waiter([&] { zx::LockScope hold(lock); });
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	release = true;
	other.join();
	waiter.join();
	EXPECT_EQ(2u, lock.Takes());
	EXPECT_EQ(1u, lock.Waits());
}
