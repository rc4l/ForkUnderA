// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/asks_compute.h"

#include <gtest/gtest.h>

#include <atomic>
#include <stdexcept>
#include <thread>

using zx::handoff::Asks;

namespace {

void AddOne(void *arg)
{
	++*(int *)arg;
}

void Throw(void *)
{
	throw std::runtime_error("font");
}

} // namespace

TEST(Asks, NothingWaitingServesNothing)
{
	Asks asks;
	EXPECT_FALSE(asks.Serve());
	EXPECT_EQ(0u, asks.Served());
}

// [rc4l] The asking thread waits until the serving thread has run its call, and sees what the call wrote.
TEST(Asks, AnAskRunsOnTheServingThreadAndReturnsAfter)
{
	Asks asks;
	int value = 0;
	std::atomic<bool> returned(false);
	std::thread asker([&] { asks.Ask(AddOne, &value); returned = true; });
	while (!asks.Serve()) std::this_thread::yield();
	asker.join();
	EXPECT_TRUE(returned);
	EXPECT_EQ(1, value);
	EXPECT_EQ(1u, asks.Served());
}

// [rc4l] A thread waiting for the game thread to finish serves its asks meanwhile, which a tic waiting on a font needs to finish at all.
TEST(Asks, ServeUntilServesAsksBeforeItsConditionHolds)
{
	Asks asks;
	int value = 0;
	std::atomic<bool> done(false);
	std::thread game([&] {
		asks.Ask(AddOne, &value);
		asks.Ask(AddOne, &value);
		done = true;
		asks.Wake();
	});
	asks.ServeUntil([&] { return done.load(); });
	game.join();
	EXPECT_EQ(2, value);
	EXPECT_EQ(2u, asks.Served());
}

TEST(Asks, ServeUntilReturnsAtOnceWhenAlreadyDone)
{
	Asks asks;
	asks.ServeUntil([] { return true; });
	EXPECT_EQ(0u, asks.Served());
}

// [rc4l] What the call throws comes out of Ask on the asking thread, as an error in an inline tic would.
TEST(Asks, WhatTheCallThrowsIsRethrownToTheAsker)
{
	Asks asks;
	bool caught = false;
	std::thread asker([&] {
		try
		{
			asks.Ask(Throw, nullptr);
		}
		catch (const std::runtime_error &)
		{
			caught = true;
		}
	});
	while (!asks.Serve()) std::this_thread::yield();
	asker.join();
	EXPECT_TRUE(caught);
}

// [rc4l] Two askers take turns, each returning only after its own call ran.
TEST(Asks, TwoAskersTakeTurns)
{
	Asks asks;
	int a = 0, b = 0;
	std::thread first([&] { asks.Ask(AddOne, &a); });
	std::thread second([&] { asks.Ask(AddOne, &b); });
	while (asks.Served() < 2) asks.Serve();
	first.join();
	second.join();
	EXPECT_EQ(1, a);
	EXPECT_EQ(1, b);
}
