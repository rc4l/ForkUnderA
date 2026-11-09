// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's own WAD folders (a WAD dump), searched subfolders and all before a server's or a preset's files are downloaded.

#ifndef ZX_WADFOLDERS_H
#define ZX_WADFOLDERS_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx { namespace wadfolders {

// [rc4l] The folders as the ini lists them, in order.
std::vector<std::string> Folders();

// [rc4l] The folders with $-variables and ~ expanded, the ones that exist, for walking and IWAD detection.
std::vector<std::string> SearchRoots();

// [rc4l] Adds a folder to the ini and re-indexes; false when it is not a folder or is listed already.
bool Add(const char *folder);

// [rc4l] Removes the folder at `index` and re-indexes; false when there is none.
bool Remove(size_t index);

// [rc4l] Asks the OS for a folder (owner: a native window handle or NULL) and adds it; false when cancelled or no picker exists.
bool PickAndAdd(void *owner);

// [rc4l] Starts indexing the folders on a worker: at startup only when there is no index yet, and with `force` (each change) abandoning a walk under way.
void StartIndex(bool force);

// [rc4l] Every indexed copy of `name`, without waiting for a walk under way, so drawing code may ask.
void FindAll(const char *name, std::vector<std::string> &out);

// [rc4l] Before a lookup that decides a download: waits for a walk under way, and rebuilds an index that has not seen `name` and is a minute old.
void PrepareLookup(const char *name);

// [rc4l] Rebuilds the FUAWadFolders option menu's items from the list.
void RefreshMenu();

}} // namespace zx::wadfolders

#endif // ZX_WADFOLDERS_H
