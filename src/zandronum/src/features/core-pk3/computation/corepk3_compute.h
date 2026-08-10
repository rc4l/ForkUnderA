// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The engine's own data pk3 is named for the build that produced it: fua_core_<version>.pk3.
//
// A fixed name loads happily when it is stale, and the engine then misbehaves somewhere far from the
// cause. Keyed, a mismatched pair is simply not found, and the startup failure can say which cores
// ARE present, which turns a dead end into a diagnosis.
//
// What lives here is the part that is a decision rather than an act: whether a filename is one of
// ours, and what to tell the player about the ones we found. The directory scan itself is the
// caller's, because it is I/O.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_COREPK3_COMPUTE_H
#define ZX_COREPK3_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// The prefix every engine data pk3 carries, and the extension.
extern const char *const kCorePk3Prefix;
extern const char *const kCorePk3Extension;

// [rc4l] Is this one of ours?
//
// Case-insensitive because Windows filesystems are, and a player who renamed a file to
// FUA_CORE_V1.PK3 meant the same thing the lowercase name means. Used twice: to pick the names worth
// reporting when the expected core is missing, and to catch a SECOND core arriving through -file,
// which would put two sets of engine lumps in play and leave the first-definition-wins rule silently
// picking a winner.
bool IsCorePk3Name(const std::string &fileName);

// [rc4l] What to say under "Cannot find <expected>".
//
// `found` is every fua_core_*.pk3 beside the executable, in whatever order the filesystem gave them.
// Returning the empty-case text rather than an empty string is deliberate: "none at all" and "the
// wrong one" are different problems with different fixes, and a blank second line would hide which
// one the player has.
std::string DescribeFoundCores(const std::string &expected, const std::vector<std::string> &found);

} // namespace zx

#endif // ZX_COREPK3_COMPUTE_H
