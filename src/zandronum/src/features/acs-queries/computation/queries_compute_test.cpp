// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-queries/computation/queries_compute.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace zx::acsqueries;

TEST(AcsQueries, EachFilterKindNamesWhatItTakes)
{
	EXPECT_EQ(ComputeFilterArg(kFqClass), kArgName);
	EXPECT_EQ(ComputeFilterArg(kFqNotClass), kArgName);
	EXPECT_EQ(ComputeFilterArg(kFqFlag), kArgName);
	for (int kind : { kFqFriendly, kFqHostile, kFqSight, kFqExclude })
		EXPECT_EQ(ComputeFilterArg(kind), kArgHandle) << kind;
	EXPECT_EQ(ComputeFilterArg(kFqTid), kArgNumber);
	for (int kind : { kFqAlive, kFqDead, kFqMonster, kFqPlayer, kFqMissile, kFqShootable, kFq2D, kFqEdges })
		EXPECT_EQ(ComputeFilterArg(kind), kArgNone) << kind;
	EXPECT_EQ(ComputeFilterArg(0), kArgInvalid);
	EXPECT_EQ(ComputeFilterArg(kFqLastKind + 1), kArgInvalid);
	EXPECT_EQ(ComputeFilterArg(kFqNearest), kArgInvalid);
}

TEST(AcsQueries, ClassAndFlagFiltersDefaultToSubclassesAndSet)
{
	EXPECT_EQ(ComputeFilterDefaultB(kFqClass), 1);
	EXPECT_EQ(ComputeFilterDefaultB(kFqFlag), 1);
	EXPECT_EQ(ComputeFilterDefaultB(kFqTid), 0);
	EXPECT_TRUE(ComputeIsShapeFilter(kFq2D));
	EXPECT_TRUE(ComputeIsShapeFilter(kFqEdges));
	EXPECT_FALSE(ComputeIsShapeFilter(kFqSight));
}

TEST(AcsQueries, SortModesAreTheirOwnNumbers)
{
	EXPECT_TRUE(ComputeSortModeValid(kFqNone));
	EXPECT_TRUE(ComputeSortModeValid(kFqNearest));
	EXPECT_TRUE(ComputeSortModeValid(kFqFarthest));
	EXPECT_FALSE(ComputeSortModeValid(kFqClass));
}

TEST(AcsQueries, CheapFiltersRunFirstAndSightLast)
{
	const std::vector<Filter> filters = { { kFqSight, 1, 0 }, { kFqFlag, 2, 1 }, { kFq2D, 0, 0 }, { kFqFriendly, 1, 0 },
		{ kFqClass, 3, 1 }, { kFqAlive, 0, 0 }, { kFqTid, 5, 0 }, { kFqEdges, 0, 0 } };
	EXPECT_EQ(ComputeQueryFilterOrder(filters), (std::vector<size_t>{ 5, 6, 4, 1, 3, 0 }));
}

TEST(AcsQueries, AMatchStopsAtTheFirstFailingFilter)
{
	const std::vector<Filter> filters = { { kFqSight, 0, 0 }, { kFqAlive, 0, 0 }, { kFqTid, 0, 0 } };
	std::vector<int> tested;
	const auto test = [&](const Filter &f) { tested.push_back(f.kind); return f.kind != kFqTid; };
	EXPECT_FALSE(ComputeQueryMatch(filters, ComputeQueryFilterOrder(filters), test));
	EXPECT_EQ(tested, (std::vector<int>{ kFqAlive, kFqTid }));
	tested.clear();
	EXPECT_TRUE(ComputeQueryMatch(filters, { 1, 0 }, test));
	EXPECT_EQ(tested, (std::vector<int>{ kFqAlive, kFqSight }));
}

TEST(AcsQueries, TheRangeIsASphereACylinderOrTheActorsEdges)
{
	EXPECT_FALSE(ComputeQueryShape({ { kFqAlive, 0, 0 } }).flat);
	const RangeShape both = ComputeQueryShape({ { kFq2D, 0, 0 }, { kFqEdges, 0, 0 } });
	EXPECT_TRUE(both.flat && both.edges);
	const double centre[3] = { 0, 0, 0 };
	const double origin[3] = { 30, 40, 120 };
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(centre, origin, 20, 56, { false, false }), std::sqrt(50.0 * 50 + 120 * 120));
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(centre, origin, 20, 56, { true, false }), 50);
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(centre, origin, 20, 56, { true, true }), 30);
	// [Doruk-Aksoy] Edges measure height to the nearest of the actor's feet and top: below, inside and above.
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(centre, origin, 20, 56, { false, true }), std::sqrt(30.0 * 30 + 120 * 120));
	const double inside[3] = { 0, 0, 130 };
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(inside, origin, 20, 56, { false, true }), 30);
	const double above[3] = { 30, 40, 200 };
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(above, origin, 20, 56, { false, true }), 24);
	// [Doruk-Aksoy] An actor whose radius covers the centre is at distance 0.
	const double near[3] = { 35, 40, 120 };
	EXPECT_DOUBLE_EQ(ComputeQueryDistance(near, origin, 20, 56, { true, true }), 0);
}

TEST(AcsQueries, SortsAreStableAndCapped)
{
	int a, b, c, d;
	std::vector<Candidate> found = { { &a, 5, 0 }, { &b, 1, 1 }, { &c, 5, 2 }, { &d, 3, 3 } };
	std::vector<Candidate> nearest = found;
	ComputeQuerySort(nearest, kFqNearest, 0);
	EXPECT_EQ(nearest[0].actor, &b);
	EXPECT_EQ(nearest[1].actor, &d);
	EXPECT_EQ(nearest[2].actor, &a);
	EXPECT_EQ(nearest[3].actor, &c);
	std::vector<Candidate> farthest = found;
	ComputeQuerySort(farthest, kFqFarthest, 2);
	ASSERT_EQ(farthest.size(), 2u);
	EXPECT_EQ(farthest[0].actor, &a);
	EXPECT_EQ(farthest[1].actor, &c);
	std::vector<Candidate> walk = found;
	ComputeQuerySort(walk, kFqNone, 3);
	ASSERT_EQ(walk.size(), 3u);
	EXPECT_EQ(walk[2].actor, &c);
}

TEST(AcsQueries, ResultsStopAtTenTwentyFour)
{
	EXPECT_EQ(ComputeResultCap(0), kMaxResults);
	EXPECT_EQ(ComputeResultCap(5000), kMaxResults);
	EXPECT_EQ(ComputeResultCap(7), 7u);
	std::vector<Candidate> many(1500, Candidate{ nullptr, 0, 0 });
	ComputeQuerySort(many, kFqNone, 0);
	EXPECT_EQ(many.size(), kMaxResults);
}

TEST(AcsQueries, IdsRunUpAndWrapToOne)
{
	EXPECT_EQ(ComputeNextQueryId(0), 1);
	EXPECT_EQ(ComputeNextQueryId(-4), 1);
	EXPECT_EQ(ComputeNextQueryId(41), 42);
	EXPECT_EQ(ComputeNextQueryId(INT32_MAX), 1);
}

TEST(AcsQueries, AScriptHoldsSixteenQueriesFoundByTheirIds)
{
	ScriptQueries qs;
	for (int32_t id = 1; id <= 16; ++id)
		EXPECT_TRUE(qs.Create(id * 10));
	EXPECT_FALSE(qs.Create(500));
	EXPECT_EQ(qs.Count(), 16u);
	EXPECT_TRUE(qs.Free(30));
	EXPECT_FALSE(qs.Free(30));
	EXPECT_EQ(qs.Find(30), nullptr);
	EXPECT_FALSE(qs.Create(40));
	EXPECT_FALSE(qs.Create(0));
	EXPECT_TRUE(qs.Create(500));
	const ScriptQueries &view = qs;
	ASSERT_NE(view.Find(500), nullptr);
	EXPECT_EQ(view.Find(500)->sort, kFqNone);
	EXPECT_EQ(ComputeHighestId(qs), 500);
	EXPECT_EQ(ComputeHighestId(ScriptQueries()), 0);
}

TEST(AcsQueries, FiltersAndSortsStayUntilCleared)
{
	ScriptQueries qs;
	ASSERT_TRUE(qs.Create(1));
	EXPECT_TRUE(qs.AddFilter(1, { kFqTid, 5, 0 }));
	EXPECT_TRUE(qs.AddFilter(1, { kFqAlive, 0, 0 }));
	EXPECT_TRUE(qs.AddFilter(1, { kFqTid, 6, 0 }));
	EXPECT_FALSE(qs.AddFilter(1, { 99, 0, 0 }));
	EXPECT_FALSE(qs.AddFilter(2, { kFqAlive, 0, 0 }));
	EXPECT_TRUE(qs.SetSort(1, kFqNearest, 4));
	EXPECT_FALSE(qs.SetSort(1, kFqTid, 4));
	EXPECT_FALSE(qs.SetSort(2, kFqNearest, 4));
	EXPECT_TRUE(qs.Clear(1, kFqTid));
	ASSERT_EQ(qs.Find(1)->filters.size(), 1u);
	EXPECT_EQ(qs.Find(1)->sort, kFqNearest);
	EXPECT_TRUE(qs.Clear(1, 0));
	EXPECT_TRUE(qs.Find(1)->filters.empty());
	EXPECT_EQ(qs.Find(1)->sort, kFqNone);
	EXPECT_EQ(qs.Find(1)->max, 0u);
	EXPECT_FALSE(qs.Clear(2, 0));
	for (size_t i = 0; i < kMaxFilters; ++i)
		EXPECT_TRUE(qs.AddFilter(1, { kFqAlive, 0, 0 }));
	EXPECT_FALSE(qs.AddFilter(1, { kFqAlive, 0, 0 }));
}

TEST(AcsQueries, QueriesSaveWithTheirResultsAndNamesRemap)
{
	ScriptQueries qs;
	ASSERT_TRUE(qs.Create(7) && qs.Create(9));
	qs.AddFilter(7, { kFqClass, 300, 1 });
	qs.AddFilter(7, { kFqTid, 300, 0 });
	qs.AddFilter(7, { kFqFlag, 301, 0 });
	qs.SetSort(7, kFqFarthest, 3);
	qs.Find(7)->results = { 1048577, 2, 3 };
	const std::vector<int32_t> flat = ComputeFlattenQueries(qs);
	ScriptQueries back;
	ASSERT_TRUE(ComputeUnflattenQueries(flat.data(), flat.size(), back));
	ASSERT_EQ(back.Count(), 2u);
	EXPECT_EQ(back.Find(7)->results, (std::vector<int32_t>{ 1048577, 2, 3 }));
	EXPECT_EQ(back.Find(7)->sort, kFqFarthest);
	EXPECT_EQ(back.Find(7)->max, 3u);
	EXPECT_TRUE(back.Find(9)->filters.empty());
	ComputeRemapQueryNames(back, [](int32_t name) { return name - 299; });
	EXPECT_EQ(back.Find(7)->filters[0].a, 1);
	EXPECT_EQ(back.Find(7)->filters[1].a, 300);
	EXPECT_EQ(back.Find(7)->filters[2].a, 2);
}

TEST(AcsQueries, ADamagedSaveLoadsNothing)
{
	ScriptQueries qs;
	ASSERT_TRUE(qs.Create(7));
	qs.AddFilter(7, { kFqTid, 5, 0 });
	qs.Find(7)->results = { 4 };
	const std::vector<int32_t> good = ComputeFlattenQueries(qs);
	ASSERT_EQ(good, (std::vector<int32_t>{ 1, 7, kFqNone, 0, 1, kFqTid, 5, 0, 1, 4 }));
	// [Doruk-Aksoy] Each damage, at the index it changes: too many queries, a bad id, a bad sort, too many filters, a bad kind, too many results.
	const struct { size_t at; int32_t value; } damage[] = { { 0, 17 }, { 1, 0 }, { 2, 5 }, { 4, 33 }, { 5, 99 }, { 8, 1025 }, { 8, 2 }, { 0, -1 } };
	for (const auto &d : damage)
	{
		std::vector<int32_t> bad = good;
		bad[d.at] = d.value;
		ScriptQueries out;
		out.Create(3);
		EXPECT_FALSE(ComputeUnflattenQueries(bad.data(), bad.size(), out)) << d.at << " " << d.value;
		EXPECT_NE(out.Find(3), nullptr);
	}
	for (size_t cut = 0; cut < good.size(); ++cut)
	{
		ScriptQueries out;
		EXPECT_FALSE(ComputeUnflattenQueries(good.data(), cut, out)) << cut;
	}
	std::vector<int32_t> extra = good;
	extra.push_back(0);
	ScriptQueries out;
	EXPECT_FALSE(ComputeUnflattenQueries(extra.data(), extra.size(), out));
	const std::vector<int32_t> twice = { 2, 7, 0, 0, 0, 0, 7, 0, 0, 0, 0 };
	EXPECT_FALSE(ComputeUnflattenQueries(twice.data(), twice.size(), out));
}
