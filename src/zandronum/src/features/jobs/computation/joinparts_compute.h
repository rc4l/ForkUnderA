// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [Doruk-Aksoy] A list a job's chunks gather side by side, each into a part of its own, joined in chunk order into the list one thread would have made.

#ifndef ZX_JOINPARTS_COMPUTE_H
#define ZX_JOINPARTS_COMPUTE_H

#include <cstddef>
#include <vector>

namespace zx { namespace jobs {

// [Doruk-Aksoy] Bytes between two parts' starts, two cache lines, so no line holds both parts' ends whatever the array's alignment.
const size_t kPartBytes = 128;

// [Doruk-Aksoy] What one chunk gathers, padded so lanes growing neighbouring parts do not pass the line holding both ends back and forth.
template <class T> struct Part
{
	std::vector<T> items;
	char pad[kPartBytes - sizeof(std::vector<T>)];
};

// [Doruk-Aksoy] A chunk's own state of at most one line, padded as a part is, so lanes working on neighbouring chunks never write one line.
template <class T> struct Padded
{
	static_assert(sizeof(T) <= kPartBytes / 2, "a padded value fits one line");
	T value;
	char pad[kPartBytes - sizeof(T)];
};

// [Doruk-Aksoy] parts sized to chunks, each emptied, so a job's chunks can gather into them.
template <class T> void ComputeEmptyParts(std::vector<Part<T>> &parts, int chunks)
{
	static_assert(sizeof(Part<T>) == kPartBytes, "a part fills its two cache lines exactly");
	parts.resize(chunks > 0 ? (size_t)chunks : 0);
	for (Part<T> &part : parts) part.items.clear();
}

// [Doruk-Aksoy] parts joined into out in order, starts[k] where part k begins in it.
template <class T> void ComputeJoinParts(const std::vector<Part<T>> &parts, std::vector<T> &out, std::vector<int> &starts)
{
	size_t total = 0;
	for (const Part<T> &part : parts) total += part.items.size();
	out.clear();
	out.reserve(total);
	starts.resize(parts.size());
	for (size_t k = 0; k < parts.size(); k++)
	{
		starts[k] = (int)out.size();
		out.insert(out.end(), parts[k].items.begin(), parts[k].items.end());
	}
}

}} // namespace zx::jobs

#endif // ZX_JOINPARTS_COMPUTE_H
