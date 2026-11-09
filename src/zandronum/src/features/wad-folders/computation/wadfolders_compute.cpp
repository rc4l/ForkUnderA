// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/wad-folders/computation/wadfolders_compute.h"
#include "features/wad-library/computation/wadlibrary_compute.h"   // [rc4l] IsLoadableWadName

#include <cctype>

namespace zx
{

namespace
{

std::string Lower(const std::string &s)
{
	std::string out = s;
	for (size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
	return out;
}

bool IsRoot(const std::string &path)
{
	return (path == "/") || ((path.size() == 3) && (path[1] == ':') && (path[2] == '/'));
}

void Walk(const std::string &dir, int depth, const WadFolderLister &list, int depthCap, size_t fileCap,
	const std::function<bool()> &cancelled, WadFolderIndex &index, bool &hitCap)
{
	if (hitCap || (depth > depthCap) || cancelled())
		return;

	std::vector<WadFolderEntry> entries;
	if (!list(dir, entries))
		return;

	const std::string prefix = IsRoot(dir) ? dir : dir + "/";
	std::vector<std::string> subdirs;
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (entries[i].directory)
		{
			subdirs.push_back(prefix + entries[i].name);
			continue;
		}
		if (!IsLoadableWadName(entries[i].name))
			continue;
		if (index.Size() >= fileCap)
		{
			hitCap = true;
			return;
		}
		index.Add(entries[i].name, prefix + entries[i].name);
	}

	for (size_t i = 0; i < subdirs.size(); ++i)
		Walk(subdirs[i], depth + 1, list, depthCap, fileCap, cancelled, index, hitCap);
}

} // namespace

std::string ComputeWadFolderNormalize(const std::string &path)
{
	std::string out = path;
	for (size_t i = 0; i < out.size(); ++i)
	{
		if (out[i] == '\\')
			out[i] = '/';
	}
	while ((out.size() > 1) && (out[out.size() - 1] == '/') && !IsRoot(out))
		out.erase(out.size() - 1);
	return out;
}

bool ComputeWadFolderSame(const std::string &a, const std::string &b, bool ignoreCase)
{
	const std::string na = ComputeWadFolderNormalize(a);
	const std::string nb = ComputeWadFolderNormalize(b);
	return ignoreCase ? (Lower(na) == Lower(nb)) : (na == nb);
}

bool ComputeWadFolderAdd(std::vector<std::string> &folders, const std::string &folder, bool ignoreCase)
{
	const std::string normal = ComputeWadFolderNormalize(folder);
	if (normal.empty())
		return false;
	for (size_t i = 0; i < folders.size(); ++i)
	{
		if (ComputeWadFolderSame(folders[i], normal, ignoreCase))
			return false;
	}
	folders.push_back(normal);
	return true;
}

bool ComputeWadFolderRemove(std::vector<std::string> &folders, size_t index)
{
	if (index >= folders.size())
		return false;
	folders.erase(folders.begin() + static_cast<std::ptrdiff_t>(index));
	return true;
}

void WadFolderIndex::Add(const std::string &name, const std::string &path)
{
	mByName[Lower(name)].push_back(path);
	mCount++;
}

void WadFolderIndex::Find(const std::string &name, std::vector<std::string> &out) const
{
	const std::unordered_map<std::string, std::vector<std::string> >::const_iterator it = mByName.find(Lower(name));
	if (it != mByName.end())
		out.insert(out.end(), it->second.begin(), it->second.end());
}

bool ComputeWadFolderWalk(const std::vector<std::string> &roots, const WadFolderLister &list,
	int depthCap, size_t fileCap, const std::function<bool()> &cancelled, WadFolderIndex &index)
{
	bool hitCap = false;
	for (size_t i = 0; i < roots.size() && !hitCap; ++i)
		Walk(ComputeWadFolderNormalize(roots[i]), 0, list, depthCap, fileCap, cancelled, index, hitCap);
	return hitCap;
}

std::string ComputeWadFolderLabel(const std::string &folder, size_t maxChars)
{
	const std::string path = ComputeWadFolderNormalize(folder);
	if (path.size() <= maxChars)
		return path;
	if (maxChars <= 3)
		return path.substr(path.size() - maxChars);

	const size_t keep = maxChars - 3;
	const size_t from = path.size() - keep;
	const size_t cut = path.find('/', from);
	if (cut == std::string::npos)
		return "..." + path.substr(from);
	return "..." + path.substr(cut);
}

bool ComputeWadFolderNeedsRefresh(bool missed, double ageSeconds, double minAgeSeconds)
{
	return missed && (ageSeconds >= minAgeSeconds);
}

} // namespace zx
