// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-download/computation/fileresolve_compute.h"

#include "features/wad-download/computation/wadstore_compute.h"

namespace zx
{

namespace
{

// Lower-cased with separators flattened, for comparing two spellings of one path. Not a real path
// canonicaliser: it settles C:\x\a.wad against c:/x/a.wad, which is the difference that actually
// reaches us, and does not pretend to resolve "..", symlinks or short names.
std::string PathKey(const std::string &path)
{
	std::string key;
	key.reserve(path.size());

	for (size_t i = 0; i < path.size(); ++i)
	{
		char c = path[i];
		if (c == '\\')
			c = '/';
		else if ((c >= 'A') && (c <= 'Z'))
			c = static_cast<char>(c - 'A' + 'a');
		key += c;
	}

	return key;
}

// `dir` is never empty: the caller checks before it builds any path inside our own folder, because
// joining onto nothing would produce a relative path that stats against the working directory.
std::string JoinDir(const std::string &dir, const std::string &rest)
{
	std::string out = dir;
	const char last = out[out.size() - 1];
	if ((last != '/') && (last != '\\'))
		out += '/';
	out += rest;
	return out;
}

// Append unless some earlier step is the same file.
void PushUnique(std::vector<ResolveStep> &steps, std::vector<std::string> &seen,
	const std::string &path, ResolveCheck check)
{
	if (path.empty())
		return;

	const std::string key = PathKey(path);
	for (size_t i = 0; i < seen.size(); ++i)
	{
		if (seen[i] == key)
			return;
	}

	seen.push_back(key);
	steps.push_back(ResolveStep(path, check));
}

} // namespace

std::vector<ResolveStep> PlanFileResolve(const std::string &name, const std::string &md5Hex,
	const std::string &downloadDir, const std::vector<std::string> &searchHits)
{
	std::vector<ResolveStep> steps;
	std::vector<std::string> seen;

	// Without a digest nothing below could ever be confirmed, so say so by returning nothing rather
	// than a plan that is guaranteed to end empty.
	if (name.empty() || !IsHexDigest(md5Hex, 32))
		return steps;

	// The digest is known good by here, so an empty relative path means the NAME is not one we would
	// build a path from, and in that case neither step below may join it onto our folder either.
	const std::string stored = StoredRelativePath(md5Hex, name, 32);
	if (!stored.empty() && !downloadDir.empty())
	{
		// The store: a stat, and the only step that stays free however large the file is.
		PushUnique(steps, seen, JoinDir(downloadDir, stored), ResolveCheck::Stat);

		// Our flat working copy, from before the store existed or after an upgrade dropped one there.
		PushUnique(steps, seen, JoinDir(downloadDir, name), ResolveCheck::Hash);
	}

	// Then wherever the engine found copies, in its order. Deduped against the two above because the
	// download folder is itself registered in FileSearch.Directories.
	for (size_t i = 0; i < searchHits.size(); ++i)
		PushUnique(steps, seen, searchHits[i], ResolveCheck::Hash);

	return steps;
}

} // namespace zx
