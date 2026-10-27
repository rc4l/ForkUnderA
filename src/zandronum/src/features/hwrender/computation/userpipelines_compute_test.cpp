// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include <memory>

#include "features/hwrender/computation/userpipelines_compute.h"

using namespace zx::hwrender;

namespace {

// [Doruk-Aksoy] A pipeline is its status, which a wait moves from compiling to ready, as the device's pool would.
typedef std::shared_ptr<int> Pso;

int StatusOf(Pso &p, bool wait)
{
	if (wait && *p == kUserPipelineCompiling) *p = kUserPipelineReady;
	return *p;
}

struct Counts { int starts = 0, finishes = 0; };

} // namespace

// [Doruk-Aksoy] Started once and drawn plain while it compiles, then finished once when ready and kept.
TEST(UserPipelines, CompilingDrawsPlainThenLandsOnce)
{
	UserPipelines<Pso> table;
	Counts n;
	Pso made;
	auto start = [&]() { n.starts++; made = std::make_shared<int>(kUserPipelineCompiling); return made; };
	auto finish = [&](const std::pair<int, int> &, Pso &p) { n.finishes++; return p != nullptr; };
	const std::pair<int, int> key(1, 7);
	EXPECT_EQ(nullptr, ComputeUserPipeline(table, key, false, start, StatusOf, finish));
	EXPECT_EQ(nullptr, ComputeUserPipeline(table, key, false, start, StatusOf, finish));
	*made = kUserPipelineReady;
	Pso *got = ComputeUserPipeline(table, key, false, start, StatusOf, finish);
	ASSERT_NE(nullptr, got);
	EXPECT_TRUE(made == *got);
	EXPECT_NE(nullptr, ComputeUserPipeline(table, key, false, start, StatusOf, finish));
	EXPECT_EQ(1, n.starts);
	EXPECT_EQ(1, n.finishes);
}

// [Doruk-Aksoy] Asked for with a wait, a compiling one lands at once.
TEST(UserPipelines, AWaitLandsAtOnce)
{
	UserPipelines<Pso> table;
	auto start = []() { return std::make_shared<int>(kUserPipelineCompiling); };
	auto finish = [](const std::pair<int, int> &, Pso &) { return true; };
	EXPECT_NE(nullptr, ComputeUserPipeline(table, std::make_pair(0, 0), true, start, StatusOf, finish));
}

// [Doruk-Aksoy] A failed pipeline, or one never made, reaches finish empty and stays out without being started again.
TEST(UserPipelines, AFailureIsFinishedEmptyAndKeptOut)
{
	UserPipelines<Pso> table;
	Counts n;
	bool sawEmpty = false;
	auto fails = [&]() { n.starts++; return std::make_shared<int>(kUserPipelineFailed); };
	auto never = [&]() { n.starts++; return Pso(); };
	auto finish = [&](const std::pair<int, int> &, Pso &p) { n.finishes++; sawEmpty = p == nullptr; return true; };
	EXPECT_EQ(nullptr, ComputeUserPipeline(table, std::make_pair(2, 3), false, fails, StatusOf, finish));
	EXPECT_TRUE(sawEmpty);
	EXPECT_EQ(nullptr, ComputeUserPipeline(table, std::make_pair(2, 3), false, fails, StatusOf, finish));
	sawEmpty = false;
	EXPECT_EQ(nullptr, ComputeUserPipeline(table, std::make_pair(2, 4), false, never, StatusOf, finish));
	EXPECT_TRUE(sawEmpty);
	EXPECT_EQ(2, n.starts);
	EXPECT_EQ(2, n.finishes);
}

// [Doruk-Aksoy] One that finish refuses, as a program taking other bindings is, is left out.
TEST(UserPipelines, ARefusedOneIsLeftOut)
{
	UserPipelines<Pso> table;
	auto start = []() { return std::make_shared<int>(kUserPipelineReady); };
	auto refuse = [](const std::pair<int, int> &, Pso &) { return false; };
	EXPECT_EQ(nullptr, ComputeUserPipeline(table, std::make_pair(5, 1), false, start, StatusOf, refuse));
	EXPECT_TRUE(table.made[std::make_pair(5, 1)].pso == nullptr);
}

// [Doruk-Aksoy] Landing them all waits for each still compiling and finishes it once; a landed one is left alone.
TEST(UserPipelines, LandingAllWaitsForEachPendingOne)
{
	UserPipelines<Pso> table;
	Counts n;
	auto start = []() { return std::make_shared<int>(kUserPipelineCompiling); };
	auto ready = []() { return std::make_shared<int>(kUserPipelineReady); };
	auto finish = [&](const std::pair<int, int> &, Pso &) { n.finishes++; return true; };
	ComputeUserPipeline(table, std::make_pair(0, 1), false, ready, StatusOf, finish);
	ComputeUserPipeline(table, std::make_pair(0, 2), false, start, StatusOf, finish);
	ComputeUserPipeline(table, std::make_pair(1, 2), false, start, StatusOf, finish);
	EXPECT_EQ(1, n.finishes);
	ComputeLandAllUserPipelines(table, StatusOf, finish);
	EXPECT_EQ(3, n.finishes);
	for (auto &m : table.made) { EXPECT_FALSE(m.second.pending); EXPECT_EQ(kUserPipelineReady, *m.second.pso); }
	ComputeLandAllUserPipelines(table, StatusOf, finish);
	EXPECT_EQ(3, n.finishes);
}
