// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_QUERIES_COMPUTE_H
#define ZX_QUERIES_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace zx { namespace acsqueries {

// [Doruk-Aksoy] The ACS function numbers, fua_ block 24040-24048.
enum Function
{
	kQueryCreate = 24040,
	kQueryFree = 24041,
	kQueryClear = 24042,
	kQueryFilter = 24043,
	kQuerySort = 24044,
	kFindActors = 24045,
	kFindActorsOfClass = 24046,
	kFoundActor = 24047,
	kFoundCount = 24048,
};

// [Doruk-Aksoy] The FQ_ filter kinds, as zcommon.bcs numbers them.
enum FilterKind
{
	kFqClass = 1,
	kFqNotClass,
	kFqAlive,
	kFqDead,
	kFqMonster,
	kFqPlayer,
	kFqMissile,
	kFqShootable,
	kFqFlag,
	kFqTid,
	kFqFriendly,
	kFqHostile,
	kFqSight,
	kFqExclude,
	kFq2D,
	kFqEdges,
	kFqLastKind = kFqEdges,
};

// [Doruk-Aksoy] The FQ_ sort modes, numbered apart from the filter kinds so one passed for the other is refused.
enum SortMode
{
	kFqNone = 0,
	kFqNearest = 32,
	kFqFarthest = 33,
};

// [Doruk-Aksoy] What a filter's first argument is.
enum ArgKind
{
	kArgInvalid,
	kArgNone,
	kArgName,
	kArgHandle,
	kArgNumber,
};

const size_t kMaxQueriesPerScript = 16;
const size_t kMaxFilters = 32;
const uint32_t kMaxResults = 1024;

struct Filter
{
	int kind;
	int32_t a;
	int32_t b;
};

struct Query
{
	int32_t id;
	std::vector<Filter> filters;
	int sort;
	uint32_t max;
	std::vector<int32_t> results;
};

// [Doruk-Aksoy] A found actor before sorting: what it is, its distance from the search centre and its walk order.
struct Candidate
{
	const void *actor;
	double distance;
	size_t order;
};

// [Doruk-Aksoy] How the search range is measured: flat ignores height, edges counts the actor's radius and height.
struct RangeShape
{
	bool flat;
	bool edges;
};

ArgKind ComputeFilterArg(int kind);

// [Doruk-Aksoy] The second argument when a call leaves it out: class filters include subclasses and FQ_FLAG wants the flag set.
int32_t ComputeFilterDefaultB(int kind);

// [Doruk-Aksoy] Whether the kind shapes the range rather than testing each actor.
bool ComputeIsShapeFilter(int kind);

bool ComputeSortModeValid(int mode);

// [Doruk-Aksoy] Indices of the per-actor filters, cheapest first and line of sight last, ties kept in the order added.
std::vector<size_t> ComputeQueryFilterOrder(const std::vector<Filter> &filters);

RangeShape ComputeQueryShape(const std::vector<Filter> &filters);

// [Doruk-Aksoy] An actor's distance from the centre in map units, by the query's shape.
double ComputeQueryDistance(const double centre[3], const double origin[3], double radius, double height, RangeShape shape);

// [Doruk-Aksoy] The result cap: 1024, or a smaller max the sort set.
uint32_t ComputeResultCap(uint32_t max);

// [Doruk-Aksoy] Sorts candidates by distance, stably, then keeps the first `cap`.
void ComputeQuerySort(std::vector<Candidate> &found, int mode, uint32_t max);

// [Doruk-Aksoy] The id after `current`, wrapping past the largest positive int to 1.
int32_t ComputeNextQueryId(int32_t current);

// [Doruk-Aksoy] Whether an actor passes every per-actor filter, tested in `order` and stopping at the first that fails.
template <class Test>
bool ComputeQueryMatch(const std::vector<Filter> &filters, const std::vector<size_t> &order, Test test)
{
	for (size_t i : order)
		if (!test(filters[i]))
			return false;
	return true;
}

// [Doruk-Aksoy] One script's queries, at most 16, found by id.
class ScriptQueries
{
public:
	// [Doruk-Aksoy] A new query with `id`, false when the script already holds 16.
	bool Create(int32_t id);
	Query *Find(int32_t id);
	const Query *Find(int32_t id) const;
	bool Free(int32_t id);

	// [Doruk-Aksoy] Kind 0 drops every filter and the sort; another kind drops only filters of that kind.
	bool Clear(int32_t id, int kind);

	// [Doruk-Aksoy] Adds a filter, false for an unknown kind, an unknown query or one already holding 32.
	bool AddFilter(int32_t id, const Filter &filter);
	bool SetSort(int32_t id, int mode, uint32_t max);

	size_t Count() const { return queries.size(); }

	std::vector<Query> queries;
};

// [Doruk-Aksoy] The largest id the script holds, 0 for none.
int32_t ComputeHighestId(const ScriptQueries &queries);

// [Doruk-Aksoy] The queries as one run of ints for a savegame, and back; decoding refuses a damaged run.
std::vector<int32_t> ComputeFlattenQueries(const ScriptQueries &queries);
bool ComputeUnflattenQueries(const int32_t *data, size_t count, ScriptQueries &queries);

// [Doruk-Aksoy] Maps every name a filter holds (class and flag names) through `map`.
template <class Map>
void ComputeRemapQueryNames(ScriptQueries &queries, Map map)
{
	for (Query &q : queries.queries)
		for (Filter &f : q.filters)
			if (ComputeFilterArg(f.kind) == kArgName)
				f.a = map(f.a);
}

}} // namespace zx::acsqueries

#endif
