// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/wadstore_compute.h"

#include <algorithm>

#include "features/wad-download/computation/downloadplan_compute.h"

namespace zx
{

const char *const kStoreDirName = "by-hash";

namespace
{

char LowerHex(char c)
{
	if ((c >= 'A') && (c <= 'F'))
		return static_cast<char>(c - 'A' + 'a');
	return c;
}

} // namespace

bool IsHexDigest(const std::string &text, size_t expectedLen)
{
	if (text.size() != expectedLen)
		return false;

	for (size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		const bool isHex = ((c >= '0') && (c <= '9')) || ((c >= 'a') && (c <= 'f')) ||
			((c >= 'A') && (c <= 'F'));
		if (!isHex)
			return false;
	}

	return true;
}

std::string StoredRelativeDir(const std::string &digestHex, size_t digestLen)
{
	std::string out;
	if (!IsHexDigest(digestHex, digestLen))
		return out;

	// One allocation: the directory name, a separator, and the digest.
	out.reserve(6 + 1 + digestLen);
	out += kStoreDirName;
	out += '/';
	for (size_t i = 0; i < digestHex.size(); ++i)
		out += LowerHex(digestHex[i]);

	return out;
}

std::string StoredRelativePath(const std::string &digestHex, const std::string &name,
	size_t digestLen)
{
	std::string out;

	// The name goes into a path we create, so it faces the same checks as any other download name --
	// no separators, no traversal, no extension the engine would not load anyway.
	if (!IsSafeDownloadName(name))
		return out;

	out = StoredRelativeDir(digestHex, digestLen);
	if (out.empty())
		return out;

	out.reserve(out.size() + 1 + name.size());
	out += '/';
	out += name;
	return out;
}

std::vector<size_t> ComputePruneOrder(const std::vector<StoreEntry> &entries, long long capBytes)
{
	std::vector<size_t> doomed;

	if (capBytes <= 0)
		return doomed;						// no cap configured

	long long total = 0;
	for (size_t i = 0; i < entries.size(); ++i)
		total += entries[i].sizeBytes;

	if (total <= capBytes)
		return doomed;						// already fits; the common case, and it allocates nothing

	std::vector<size_t> order;
	order.reserve(entries.size());
	for (size_t i = 0; i < entries.size(); ++i)
		order.push_back(i);

	// Stable, so equal timestamps fall back to insertion order and the answer is reproducible --
	// a prune that picked differently on each run would be untestable.
	const std::vector<StoreEntry> &byIndex = entries;
	std::stable_sort(order.begin(), order.end(),
		[&byIndex](size_t a, size_t b) { return byIndex[a].lastUsedMs < byIndex[b].lastUsedMs; });

	for (size_t i = 0; (i < order.size()) && (total > capBytes); ++i)
	{
		doomed.push_back(order[i]);
		total -= entries[order[i]].sizeBytes;
	}

	return doomed;
}

} // namespace zx
