// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-queries/computation/queries_compute.h"

#include <algorithm>
#include <cmath>

namespace zx { namespace acsqueries {

namespace {

// [Doruk-Aksoy] A per-actor filter's cost: plain fields, then class and flag lookups, then relations, then line of sight.
int Cost(int kind)
{
	switch (kind)
	{
	case kFqClass:
	case kFqNotClass:
		return 1;
	case kFqFlag:
		return 2;
	case kFqFriendly:
	case kFqHostile:
		return 3;
	case kFqSight:
		return 9;
	default:
		return 0;
	}
}

// [Doruk-Aksoy] How far `v` lies outside [lo, hi], 0 inside.
double Outside(double v, double lo, double hi)
{
	return v < lo ? lo - v : v > hi ? v - hi : 0.0;
}

} // namespace

ArgKind ComputeFilterArg(int kind)
{
	switch (kind)
	{
	case kFqClass:
	case kFqNotClass:
	case kFqFlag:
		return kArgName;
	case kFqFriendly:
	case kFqHostile:
	case kFqSight:
	case kFqExclude:
		return kArgHandle;
	case kFqTid:
		return kArgNumber;
	case kFqAlive:
	case kFqDead:
	case kFqMonster:
	case kFqPlayer:
	case kFqMissile:
	case kFqShootable:
	case kFq2D:
	case kFqEdges:
		return kArgNone;
	default:
		return kArgInvalid;
	}
}

int32_t ComputeFilterDefaultB(int kind)
{
	return ComputeFilterArg(kind) == kArgName ? 1 : 0;
}

bool ComputeIsShapeFilter(int kind)
{
	return kind == kFq2D || kind == kFqEdges;
}

bool ComputeSortModeValid(int mode)
{
	return mode == kFqNone || mode == kFqNearest || mode == kFqFarthest;
}

std::vector<size_t> ComputeQueryFilterOrder(const std::vector<Filter> &filters)
{
	std::vector<size_t> order;
	for (size_t i = 0; i < filters.size(); ++i)
		if (!ComputeIsShapeFilter(filters[i].kind))
			order.push_back(i);
	std::stable_sort(order.begin(), order.end(), [&](size_t l, size_t r) { return Cost(filters[l].kind) < Cost(filters[r].kind); });
	return order;
}

RangeShape ComputeQueryShape(const std::vector<Filter> &filters)
{
	RangeShape shape = { false, false };
	for (const Filter &f : filters)
	{
		shape.flat |= f.kind == kFq2D;
		shape.edges |= f.kind == kFqEdges;
	}
	return shape;
}

double ComputeQueryDistance(const double centre[3], const double origin[3], double radius, double height, RangeShape shape)
{
	double flat = std::hypot(origin[0] - centre[0], origin[1] - centre[1]);
	double up = origin[2] - centre[2];
	if (shape.edges)
	{
		flat = std::max(0.0, flat - radius);
		up = Outside(centre[2], origin[2], origin[2] + height);
	}
	return shape.flat ? flat : std::hypot(flat, up);
}

uint32_t ComputeResultCap(uint32_t max)
{
	return max == 0 || max > kMaxResults ? kMaxResults : max;
}

void ComputeQuerySort(std::vector<Candidate> &found, int mode, uint32_t max)
{
	if (mode == kFqNearest || mode == kFqFarthest)
	{
		const bool nearest = mode == kFqNearest;
		std::stable_sort(found.begin(), found.end(), [nearest](const Candidate &l, const Candidate &r) {
			return nearest ? l.distance < r.distance : l.distance > r.distance;
		});
	}
	const uint32_t cap = ComputeResultCap(max);
	if (found.size() > cap)
		found.resize(cap);
}

int32_t ComputeNextQueryId(int32_t current)
{
	return current <= 0 || current == INT32_MAX ? 1 : current + 1;
}

bool ScriptQueries::Create(int32_t id)
{
	if (id <= 0 || queries.size() >= kMaxQueriesPerScript || Find(id) != nullptr)
		return false;
	Query q;
	q.id = id;
	q.sort = kFqNone;
	q.max = 0;
	queries.push_back(q);
	return true;
}

Query *ScriptQueries::Find(int32_t id)
{
	for (Query &q : queries)
		if (q.id == id)
			return &q;
	return nullptr;
}

const Query *ScriptQueries::Find(int32_t id) const
{
	return const_cast<ScriptQueries *>(this)->Find(id);
}

bool ScriptQueries::Free(int32_t id)
{
	for (size_t i = 0; i < queries.size(); ++i)
		if (queries[i].id == id)
		{
			queries.erase(queries.begin() + i);
			return true;
		}
	return false;
}

bool ScriptQueries::Clear(int32_t id, int kind)
{
	Query *q = Find(id);
	if (q == nullptr)
		return false;
	if (kind == 0)
	{
		q->filters.clear();
		q->sort = kFqNone;
		q->max = 0;
		return true;
	}
	q->filters.erase(std::remove_if(q->filters.begin(), q->filters.end(), [kind](const Filter &f) { return f.kind == kind; }), q->filters.end());
	return true;
}

bool ScriptQueries::AddFilter(int32_t id, const Filter &filter)
{
	Query *q = Find(id);
	if (q == nullptr || ComputeFilterArg(filter.kind) == kArgInvalid || q->filters.size() >= kMaxFilters)
		return false;
	q->filters.push_back(filter);
	return true;
}

bool ScriptQueries::SetSort(int32_t id, int mode, uint32_t max)
{
	Query *q = Find(id);
	if (q == nullptr || !ComputeSortModeValid(mode))
		return false;
	q->sort = mode;
	q->max = max;
	return true;
}

int32_t ComputeHighestId(const ScriptQueries &queries)
{
	int32_t highest = 0;
	for (const Query &q : queries.queries)
		highest = std::max(highest, q.id);
	return highest;
}

std::vector<int32_t> ComputeFlattenQueries(const ScriptQueries &queries)
{
	std::vector<int32_t> out;
	out.push_back(int32_t(queries.queries.size()));
	for (const Query &q : queries.queries)
	{
		out.push_back(q.id);
		out.push_back(q.sort);
		out.push_back(int32_t(q.max));
		out.push_back(int32_t(q.filters.size()));
		for (const Filter &f : q.filters)
		{
			out.push_back(f.kind);
			out.push_back(f.a);
			out.push_back(f.b);
		}
		out.push_back(int32_t(q.results.size()));
		out.insert(out.end(), q.results.begin(), q.results.end());
	}
	return out;
}

bool ComputeUnflattenQueries(const int32_t *data, size_t count, ScriptQueries &queries)
{
	size_t at = 0;
	// [Doruk-Aksoy] The next int, if one is left.
	const auto any = [&](int32_t &value) {
		if (at >= count)
			return false;
		value = data[at++];
		return true;
	};
	// [Doruk-Aksoy] The next int, if one is left and it lies in [lo, hi].
	const auto take = [&](int32_t lo, int32_t hi, int32_t &value) {
		return any(value) && value >= lo && value <= hi;
	};
	ScriptQueries read;
	int32_t n;
	if (!take(0, int32_t(kMaxQueriesPerScript), n))
		return false;
	for (int32_t i = 0; i < n; ++i)
	{
		int32_t id, sort, max, filters, results;
		if (!any(id) || !any(sort) || !any(max) || !read.Create(id) || !read.SetSort(id, sort, uint32_t(max)))
			return false;
		if (!take(0, int32_t(kMaxFilters), filters))
			return false;
		for (int32_t j = 0; j < filters; ++j)
		{
			Filter f;
			if (!any(f.kind) || !any(f.a) || !any(f.b) || !read.AddFilter(id, f))
				return false;
		}
		if (!take(0, int32_t(kMaxResults), results) || count - at < size_t(results))
			return false;
		read.Find(id)->results.assign(data + at, data + at + results);
		at += size_t(results);
	}
	if (at != count)
		return false;
	queries = read;
	return true;
}

}} // namespace zx::acsqueries
