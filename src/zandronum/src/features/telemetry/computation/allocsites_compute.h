// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Live allocations by the call stack that made them, for finding what a repeated action leaves behind (fua_allocsites, allocwatch.cpp), with no engine in it.

#ifndef ZX_ALLOCSITES_COMPUTE_H
#define ZX_ALLOCSITES_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zx { namespace telemetry {

// [rc4l] Recorded allocations and their interned stacks in tables sized when made, so recording from inside operator new never allocates.
class AllocTable
{
public:
	static const int kFrames = 16;
	// [rc4l] Room for liveCapacity allocations and stackCapacity distinct stacks, each rounded up to a power of two and kept three quarters full at most.
	AllocTable(size_t liveCapacity, size_t stackCapacity);
	// [rc4l] Records bytes allocated at ptr under depth frames (past kFrames dropped), replacing any record at ptr; false, counted as dropped, when a table is full.
	bool Insert(const void *ptr, size_t bytes, void *const *frames, int depth);
	// [rc4l] Forgets the allocation at ptr, if it is recorded.
	void Erase(const void *ptr);
	size_t Live() const { return m_live; }
	size_t Dropped() const { return m_dropped; }

	struct Site { std::vector<void *> frames; size_t count; double bytes; };
	// [rc4l] The top stacks by live allocations, most first, and the total live bytes in bytes.
	std::vector<Site> TopSites(size_t top, double &bytes) const;

private:
	struct Entry { const void *ptr; uint64_t bytes; uint32_t stack; };
	struct Stack { uint64_t hash; int depth; void *frames[kFrames]; };
	size_t Find(const void *ptr) const;
	bool Intern(void *const *frames, int depth, uint32_t &index);
	std::vector<Entry> m_entries;
	std::vector<Stack> m_stacks;
	size_t m_live, m_stackCount, m_dropped;
};

// [rc4l] A pointer's home slot in a table of mask + 1 slots: its alignment bits shifted out and the rest mixed.
size_t ComputeAllocHome(const void *ptr, size_t mask);

// [rc4l] A stack's frames, named from the innermost, as one line: the allocator's own frames (operator new, malloc, std::, the counter's Allocate and the stack capture) left out, empty names shown as "?", and at most shown frames kept.
std::string ComputeAllocStackText(const std::vector<std::string> &names, size_t shown);

}} // namespace zx::telemetry

#endif // ZX_ALLOCSITES_COMPUTE_H
