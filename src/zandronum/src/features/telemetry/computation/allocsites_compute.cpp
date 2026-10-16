// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/telemetry/computation/allocsites_compute.h"

#include <algorithm>

namespace zx { namespace telemetry {

namespace {

size_t PowerOfTwo(size_t n)
{
	size_t p = 1;
	while (p < n) p <<= 1;
	return p;
}

// [rc4l] FNV-1a over the frames, never zero, which marks an empty stack slot.
uint64_t StackHash(void *const *frames, int depth)
{
	uint64_t h = 1469598103934665603ull;
	for (int i = 0; i < depth; i++) h = (h ^ (uint64_t)(uintptr_t)frames[i]) * 1099511628211ull;
	return h | 1;
}

bool IsAllocatorFrame(const std::string &name)
{
	static const char *const kPrefixes[] = { "operator new", "malloc", "std::", "zx::telemetry::CaptureStack" };
	for (const char *p : kPrefixes) if (name.compare(0, std::char_traits<char>::length(p), p) == 0) return true;
	return name.find("::Allocate") != std::string::npos;
}

} // namespace

// [rc4l] Defined once here, since a test that takes it by reference needs it to exist outside the class.
const int AllocTable::kFrames;

size_t ComputeAllocHome(const void *ptr, size_t mask)
{
	const uint64_t x = ((uint64_t)(uintptr_t)ptr >> 4) * 0x9E3779B97F4A7C15ull;
	return (size_t)(x >> 20) & mask;
}

AllocTable::AllocTable(size_t liveCapacity, size_t stackCapacity)
	: m_entries(PowerOfTwo(liveCapacity), Entry{ nullptr, 0, 0 }), m_stacks(PowerOfTwo(stackCapacity), Stack{}), m_live(0), m_stackCount(0), m_dropped(0)
{
}

size_t AllocTable::Find(const void *ptr) const
{
	const size_t mask = m_entries.size() - 1;
	for (size_t i = ComputeAllocHome(ptr, mask);; i = (i + 1) & mask)
	{
		if (m_entries[i].ptr == ptr) return i;
		if (m_entries[i].ptr == nullptr) return m_entries.size();
	}
}

bool AllocTable::Intern(void *const *frames, int depth, uint32_t &index)
{
	const uint64_t hash = StackHash(frames, depth);
	const size_t mask = m_stacks.size() - 1;
	for (size_t i = (size_t)(hash >> 7) & mask;; i = (i + 1) & mask)
	{
		Stack &s = m_stacks[i];
		if (s.hash == 0)
		{
			if ((m_stackCount + 1) * 4 > m_stacks.size() * 3) return false;
			s.hash = hash;
			s.depth = depth;
			std::copy(frames, frames + depth, s.frames);
			m_stackCount++;
			index = (uint32_t)i;
			return true;
		}
		if (s.hash == hash && s.depth == depth && std::equal(frames, frames + depth, s.frames)) { index = (uint32_t)i; return true; }
	}
}

bool AllocTable::Insert(const void *ptr, size_t bytes, void *const *frames, int depth)
{
	depth = std::max(0, std::min(depth, kFrames));
	uint32_t stack = 0;
	const size_t at = Find(ptr);
	if (!Intern(frames, depth, stack) || (at == m_entries.size() && (m_live + 1) * 4 > m_entries.size() * 3))
	{
		m_dropped++;
		return false;
	}
	if (at != m_entries.size()) { m_entries[at] = Entry{ ptr, bytes, stack }; return true; }
	const size_t mask = m_entries.size() - 1;
	size_t i = ComputeAllocHome(ptr, mask);
	while (m_entries[i].ptr != nullptr) i = (i + 1) & mask;
	m_entries[i] = Entry{ ptr, bytes, stack };
	m_live++;
	return true;
}

void AllocTable::Erase(const void *ptr)
{
	size_t i = Find(ptr);
	if (i == m_entries.size()) return;
	const size_t mask = m_entries.size() - 1;
	m_entries[i].ptr = nullptr;
	m_live--;
	// [rc4l] Backward-shift deletion: each later entry of the run whose home is not cyclically in (i, j] moves into the hole, so no lookup ever stops short.
	for (size_t j = (i + 1) & mask; m_entries[j].ptr != nullptr; j = (j + 1) & mask)
	{
		const size_t home = ComputeAllocHome(m_entries[j].ptr, mask);
		const bool stays = i < j ? (home > i && home <= j) : (home > i || home <= j);
		if (stays) continue;
		m_entries[i] = m_entries[j];
		m_entries[j].ptr = nullptr;
		i = j;
	}
}

std::vector<AllocTable::Site> AllocTable::TopSites(size_t top, double &bytes) const
{
	std::vector<size_t> counts(m_stacks.size(), 0);
	std::vector<double> sizes(m_stacks.size(), 0.0);
	bytes = 0.0;
	for (const Entry &e : m_entries)
	{
		if (e.ptr == nullptr) continue;
		counts[e.stack]++;
		sizes[e.stack] += (double)e.bytes;
		bytes += (double)e.bytes;
	}
	std::vector<Site> sites;
	for (size_t i = 0; i < m_stacks.size(); i++)
		if (counts[i] != 0) sites.push_back(Site{ std::vector<void *>(m_stacks[i].frames, m_stacks[i].frames + m_stacks[i].depth), counts[i], sizes[i] });
	std::sort(sites.begin(), sites.end(), [](const Site &a, const Site &b) { return a.count != b.count ? a.count > b.count : a.bytes > b.bytes; });
	if (sites.size() > top) sites.resize(top);
	return sites;
}

std::string ComputeAllocStackText(const std::vector<std::string> &names, size_t shown)
{
	std::string out;
	size_t kept = 0;
	for (const std::string &name : names)
	{
		if (kept == shown) break;
		if (!name.empty() && IsAllocatorFrame(name)) continue;
		out += (kept++ ? " <- " : "") + (name.empty() ? std::string("?") : name);
	}
	return out.empty() ? std::string("(no frames)") : out;
}

}} // namespace zx::telemetry
