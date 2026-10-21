// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Everything a list of the player's own WADs decides, with no filesystem and no engine.
//
// The problem this is shaped by: somebody can have twenty thousand files. Three things follow, and
// all three are decisions rather than mechanism, which is why they live here.
//
// WHAT WE MAY KNOW ABOUT A FILE. Only what a directory listing gives: name, size, modified time,
// and the path it was found at. NOT its md5, and NOT what is inside it. Hashing twenty thousand
// files means reading tens of gigabytes, and opening each one to read its lump directory is twenty
// thousand opens; either turns "show me my WADs" into a minute of disk. A file is hashed when it is
// CHOSEN, one at a time, and never before.
//
// WHICH ONES ARE THE SAME FILE. Two copies of one WAD in two folders should be one row, and telling
// that reliably needs the hash we just refused to take. So the test is NAME AND SIZE, which is
// wrong only for two different files that share a name and are byte-for-byte the same length, and
// costs nothing because the listing already gave us both. Same name and a DIFFERENT size is two
// rows, deliberately: that is the case where the player has two builds of one mod and has to be
// able to see which is which.
//
// WHAT ORDER THEY COME IN. Sorted once, when the scan finishes, never per frame.
//
// The search is the other half. A key is folded once per file at scan time rather than per
// keystroke, because the cost that matters is the one paid twenty thousand times.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_WADLIBRARY_COMPUTE_H
#define ZX_WADLIBRARY_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// One file as the listing found it. Deliberately small and deliberately ignorant: everything here
// comes from the directory entry, so a scan never opens anything.
struct LibraryFile
{
	std::string path;		// full path, which is what actually gets loaded
	std::string name;		// bare filename, as the player recognises it
	std::string folder;		// the containing directory's own name, to tell copies apart on screen
	std::string key;		// `name` folded for search; see SearchFold
	long long size;
	long long mtime;

	LibraryFile() : size(0), mtime(0) {}
};

// A row on screen: one file, plus however many other copies of it were found.
struct LibraryRow
{
	size_t index;			// into the scanned vector; the copy we would actually load
	int copies;				// 1 when it was found once

	LibraryRow() : index(0), copies(1) {}
	LibraryRow(size_t i, int c) : index(i), copies(c) {}
};

// Whether a filename is one we would offer at all.
//
// Extension only, because that is all a listing gives us and opening the file to find out is the
// thing this whole unit exists to avoid.
bool IsLoadableWadName(const std::string &name);

// [rc4l] Files that are ours, or that belong to a different picker, and must never appear.
//
// The engine's own pk3s would load twice, and an IWAD is the OTHER list on this screen -- offering
// one here would let somebody build a server with two IWADs and no warning.
bool IsEngineOwnedName(const std::string &name);
bool IsKnownIwadName(const std::string &name);

// [rc4l] Every IWAD filename this engine knows, which is the SAME table IsKnownIwadName tests.
//
// Exposed because two different screens need it from opposite directions: this list hides them from
// the PWAD list, and the IWAD picker beside it has to offer them. Kept as one table so the two can
// never disagree about what an IWAD is -- a name in one and not the other is a file that is either
// offered twice or offered nowhere.
const std::vector<std::string> &KnownIwadNames();

// The search key for a name, and for a query. Lower case, and punctuation that people type
// inconsistently is dropped, so "brutalv21" finds "Brutal_v21.pk3".
std::string SearchFold(const std::string &text);

// Does this file answer `key`? An empty key matches everything, which is what an empty search box
// means. Substring rather than prefix: mod names in the wild carry version suffixes and author
// prefixes, so a player searching "sunder" must find "3-sunder-2407.wad".
bool LibraryMatches(const LibraryFile &file, const std::string &key);

// [rc4l] The order rows come out in: folded name, then size, then path, which makes it total so two
// copies of one file always land the same way round whatever order they were found in.
//
// Exposed so the SCANNER can put its files in exactly this order once, on its worker, and
// BuildLibraryRows can then skip sorting entirely. If these two ever disagreed the skip would stop
// firing silently and every keystroke would re-sort the collection, which is why there is one
// comparison rather than two that happen to match.
bool LibraryFileLess(const LibraryFile &a, const LibraryFile &b);

// [rc4l] The rows for a scanned set: filtered by `key`, deduplicated, and sorted by name.
//
// Deduplication keeps the FIRST copy in scan order and counts the rest. Scan order is search-path
// order, so the copy that wins here is the copy the engine's own name search would have found, and
// the two cannot disagree about which file "map01.wad" means.
std::vector<LibraryRow> BuildLibraryRows(const std::vector<LibraryFile> &files,
	const std::string &key);

// How many files a scan may take before it gives up, and how deep it may recurse.
//
// Both exist because a search path can contain a drive root. The cap is a number the caller reports
// rather than one it hides: a list silently missing half of somebody's collection is worse than a
// list that says it stopped.
size_t LibraryFileCap();
int LibraryDepthCap();

} // namespace zx

#endif // ZX_WADLIBRARY_COMPUTE_H
