// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the browser's crash reports say about the game, which the engine hands the page as JSON, since the page builds and sends a report after the engine itself has stopped.

#ifndef ZX_CRASHCONTEXT_COMPUTE_H
#define ZX_CRASHCONTEXT_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

namespace zx { namespace web {

struct CrashContext
{
	std::string release, dist, dsn, map;
	bool reporting = false;
	std::vector<std::string> files;
	std::vector<std::pair<std::string, std::string>> tags;
};

// [rc4l] The tracker's DSN when this build may send its reports (an official one, whose symbols are published), else empty.
std::string ComputeCrashDsn(bool official, const char *dsn);

// [rc4l] The load order as bare file names (ComputeSafeFileLabel), since a path names the player's own folders.
std::vector<std::string> ComputeCrashFiles(const std::vector<std::string> &paths);

// [rc4l] Sets a tag, replacing the value already set under its key.
void ComputeSetCrashTag(CrashContext &context, const std::string &key, const std::string &value);

// [rc4l] The context as one JSON object: release, dist, dsn, reporting, map, iwad (the second file, after the engine's own), files and tags.
std::string ComputeCrashContextJson(const CrashContext &context);

}} // namespace zx::web

#endif // ZX_CRASHCONTEXT_COMPUTE_H
