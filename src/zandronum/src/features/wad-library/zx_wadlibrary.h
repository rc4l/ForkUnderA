// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The player's own WAD collection, listed without stopping the game.
//
// WHY THIS IS NOT DONE AT STARTUP. Somebody can have twenty thousand files across a handful of
// folders. Enumerating that is a fraction of a second; opening each one, or hashing it, is minutes.
// So nothing happens until the screen that needs the list is opened, and even then the scan is a
// detached worker that the menu draws around rather than waits for.
//
// THE THREADING CONTRACT IS features/wad-download's, VERBATIM, and for the same reason. The worker
// touches nothing the engine considers single-threaded: no Printf, no CVARs, no FString, no wad
// tables. It is handed a list of directories to walk before it starts, and everything it finds goes
// into a mutex-guarded queue that Tick() drains on the main thread. Printf off the main thread has
// crashed this engine before; it is not a rule to relax.
//
// WHAT IT COLLECTS is only what a directory entry gives: path, name, size, modified time. See
// computation/wadlibrary_compute.h for why that limit is the whole design, and what it costs.

#ifndef ZX_WADLIBRARY_H
#define ZX_WADLIBRARY_H

#include <string>
#include <vector>

#include "features/wad-library/computation/wadlibrary_compute.h"

namespace zx { namespace wadlibrary {

enum class ScanState
{
	Idle,		// never asked
	Running,
	Done,
	Failed,		// nothing to walk: no search path at all
};

// Start a scan if one has not been run, or force a fresh one. Cheap and safe to call every frame:
// without `force` it returns immediately once a scan has finished or is already under way.
void Begin(bool force);

// Abandon a running scan. The worker checks between directories, so this returns at once and the
// thread unwinds on its own -- the same shape as the downloader's Abandon.
void Cancel();

ScanState State();

// How many files have been accepted so far, for a line the player can watch while it works.
size_t Found();

// True when the scan stopped because it hit LibraryFileCap. Said out loud rather than swallowed: a
// list quietly missing half a collection is worse than one that admits where it stopped.
bool HitCap();

// Drain the worker's queue into the collection. Main thread only, once a frame.
void Tick();

// Everything found, in scan order, which is search-path order. Only meaningful once State() is Done,
// and safe to read while Running -- it only ever grows.
const std::vector<LibraryFile> &Files();

// [rc4l] The md5 of one file, computed now and remembered.
//
// This is the ONLY place the library reads a file's contents, and it is called when a file is
// chosen rather than when it is found. Twenty thousand hashes is tens of gigabytes; one is a few
// milliseconds. The answer is cached against path+size+mtime, so choosing the same file again in a
// later session costs nothing and hosting never rehashes what it just hashed.
//
// Empty when the file could not be read.
std::string HashOf(const LibraryFile &file);

}} // namespace zx::wadlibrary

#endif // ZX_WADLIBRARY_H
