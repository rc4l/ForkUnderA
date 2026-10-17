// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/handoff_compute.h"

#include <algorithm>
#include <cstdio>

namespace zx { namespace handoff {

bool ComputeOwnerAllows(Owner owner, bool onMain, bool onGame, Phase phase)
{
	switch (owner)
	{
	case Owner::Main: return onMain;
	case Owner::GameThread: return onGame;
	case Owner::GameData: return (onGame && phase.gameRunning) || (onMain && phase.mainInHandoff && !phase.gameRunning);
	case Owner::Handoff: return onMain && phase.mainInHandoff && !phase.gameRunning;
	case Owner::Idle: return onMain && !phase.gameRunning;
	default: return true;
	}
}

bool ComputeBesideTic(bool onMain, bool onGame, Phase phase)
{
	return onMain && !onGame && phase.gameRunning;
}

const char *ComputeOwnerName(Owner owner)
{
	switch (owner)
	{
	case Owner::Main: return "main";
	case Owner::GameThread: return "game";
	case Owner::GameData: return "game data";
	case Owner::Handoff: return "handoff";
	case Owner::Idle: return "idle";
	default: return "any";
	}
}

uint32_t ComputeStressDelayUs(uint64_t seed, uint32_t seam, uint64_t count, uint32_t maxUs)
{
	if (maxUs == 0) return 0;
	// [rc4l] splitmix64 over the three inputs, so a run with the same seed delays the same entries.
	uint64_t z = seed + 0x9E3779B97F4A7C15ull * (count + 1) + ((uint64_t)seam << 32);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	z ^= z >> 31;
	if ((z & 7) != 0) return 0;
	return (uint32_t)((z >> 3) % ((uint64_t)maxUs + 1));
}

int SeamRegistry::Declare(const char *name, Owner owner)
{
	std::lock_guard<std::mutex> hold(m_mutex);
	for (size_t i = 0; i < m_seams.size(); i++)
		if (m_seams[i].name == name) return m_seams[i].owner == owner ? (int)i : -1;
	m_seams.emplace_back(name, owner);
	return (int)m_seams.size() - 1;
}

void SeamRegistry::Enter(int seam, bool allowed)
{
	SeamInfo &info = m_seams[seam];
	info.entries.fetch_add(1, std::memory_order_relaxed);
	if (!allowed) info.violations.fetch_add(1, std::memory_order_relaxed);
}

int SeamRegistry::Size() const
{
	std::lock_guard<std::mutex> hold(m_mutex);
	return (int)m_seams.size();
}

const SeamInfo &SeamRegistry::At(int seam) const
{
	std::lock_guard<std::mutex> hold(m_mutex);
	return m_seams[seam];
}

uint64_t SeamRegistry::Violations() const
{
	std::lock_guard<std::mutex> hold(m_mutex);
	uint64_t total = 0;
	for (const SeamInfo &info : m_seams) total += info.violations.load(std::memory_order_relaxed);
	return total;
}

std::string ComputeSeamTable(const SeamRegistry &registry)
{
	std::string out;
	char line[256];
	for (int i = 0; i < registry.Size(); i++)
	{
		const SeamInfo &info = registry.At(i);
		snprintf(line, sizeof(line), "%-32s %-9s %12llu entries %8llu violations\n", info.name.c_str(), ComputeOwnerName(info.owner),
			(unsigned long long)info.entries.load(), (unsigned long long)info.violations.load());
		out += line;
	}
	return out;
}

void ViolationSites::Count(const void *site, const char *seam, const char *function, int line, int thread, Phase phase)
{
	std::lock_guard<std::mutex> hold(m_mutex);
	for (Row &r : m_rows)
	{
		if (r.site == site && r.thread == thread && r.phase.gameRunning == phase.gameRunning && r.phase.mainInHandoff == phase.mainInHandoff)
		{
			r.count++;
			return;
		}
	}
	char where[160];
	snprintf(where, sizeof(where), "%s:%d", function, line);
	m_rows.push_back(Row{ site, seam, where, thread, phase, 1 });
}

std::string ViolationSites::Table() const
{
	std::lock_guard<std::mutex> hold(m_mutex);
	std::deque<const Row *> rows;
	for (const Row &r : m_rows) rows.push_back(&r);
	std::stable_sort(rows.begin(), rows.end(), [](const Row *a, const Row *b) { return a->count > b->count; });
	static const char *const kThreads[] = { "main", "game", "another" };
	std::string out;
	char line[320];
	for (const Row *r : rows)
	{
		snprintf(line, sizeof(line), "  violated %s in %s by the %s thread, %s, main thread %s: %llu\n", r->seam.c_str(), r->where.c_str(), kThreads[r->thread],
			r->phase.gameRunning ? "a tic running" : "no tic running", r->phase.mainInHandoff ? "in the handoff" : "drawing", (unsigned long long)r->count);
		out += line;
	}
	return out;
}

}} // namespace zx::handoff
