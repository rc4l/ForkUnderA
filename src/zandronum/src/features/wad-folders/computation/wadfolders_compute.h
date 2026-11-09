// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's WAD folders: the list kept in the ini, and the name index a recursive walk of them builds.

#ifndef ZX_WADFOLDERS_COMPUTE_H
#define ZX_WADFOLDERS_COMPUTE_H

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace zx
{

// [rc4l] A folder as the list keeps it: forward slashes, no trailing separator except a root's ("/", "C:/").
std::string ComputeWadFolderNormalize(const std::string &path);

// [rc4l] Whether two folders are one, folding case where the disk does (Windows).
bool ComputeWadFolderSame(const std::string &a, const std::string &b, bool ignoreCase);

// [rc4l] Appends `folder` normalized unless it is empty or already listed, and says whether it did.
bool ComputeWadFolderAdd(std::vector<std::string> &folders, const std::string &folder, bool ignoreCase);

// [rc4l] Drops the folder at `index`, and says whether there was one.
bool ComputeWadFolderRemove(std::vector<std::string> &folders, size_t index);

// [rc4l] Every loadable file under the folders, by lower-case name, in walk order.
class WadFolderIndex
{
public:
	void Add(const std::string &name, const std::string &path);
	void Find(const std::string &name, std::vector<std::string> &out) const;
	size_t Size() const { return mCount; }

private:
	std::unordered_map<std::string, std::vector<std::string> > mByName;
	size_t mCount = 0;
};

// [rc4l] One entry a folder listing yields.
struct WadFolderEntry
{
	std::string name;
	bool directory;
};

// [rc4l] Lists `dir`'s entries (without "." and ".."), returning false when it cannot be read.
typedef std::function<bool(const std::string &dir, std::vector<WadFolderEntry> &out)> WadFolderLister;

// [rc4l] Indexes every loadable file under `roots` down to `depthCap`, stopping at `fileCap` files or when `cancelled` says so; returns whether the cap stopped it.
bool ComputeWadFolderWalk(const std::vector<std::string> &roots, const WadFolderLister &list,
	int depthCap, size_t fileCap, const std::function<bool()> &cancelled, WadFolderIndex &index);

// [rc4l] A folder as a menu row shows it in `maxChars`: whole, or "..." and its tail from a separator on, since the tail is what tells two folders apart.
std::string ComputeWadFolderLabel(const std::string &folder, size_t maxChars);

// [rc4l] Whether a lookup that found nothing should rebuild the index first: only when it is older than `minAgeSeconds`, so one join's files rebuild it once.
bool ComputeWadFolderNeedsRefresh(bool missed, double ageSeconds, double minAgeSeconds);

} // namespace zx

#endif // ZX_WADFOLDERS_COMPUTE_H
