// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Where a downloaded file is filed, so that two files called test.wad can both exist.
//
// Storing downloads flat, by filename, has one bug and one annoyance. The bug: a player who already
// owns a test.wad earlier in FileSearch.Directories shadows the copy we just downloaded -- we detect
// the mismatch, fetch the right file, re-resolve by NAME, find theirs again, and the loop guard turns
// that into "can't join" with the correct file sitting on disk. The annoyance: alternating between
// two servers whose mods share a common name re-downloads on every single join, forever.
//
// Both go away if the path carries the content rather than just the name:
//
//     Downloads/by-hash/<md5>/test.wad
//
// Four different test.wads coexist, nothing is ever clobbered, rollback is free because the old
// version was never deleted, and -- the part that fixes the bug -- resolution stops being a search.
// When a server tells us the digest it runs, we build that exact path and hand it to the loader, so
// no other copy anywhere on the disk can win instead.
//
// THE NAME STAYS IN THE PATH, deliberately. Keyed on the digest alone, two files with the same hash
// would be one entry. Accidental MD5 collisions are not a real concern -- 128 bits means a birthday
// bound around 2^64, against an ecosystem of a few hundred thousand WADs -- but DELIBERATE ones are:
// chosen-prefix MD5 collisions are practical, which is exactly why the IWAD allowlist gate uses
// SHA-256 instead. MD5 is not our choice here; it is what the launcher protocol carries, so it is
// what we must compare against. Requiring the name to match as well means a collision alone buys
// nothing, and what it could buy is a file the player was going to download anyway.
//
// LOOSE FILES STILL WORK. A store miss falls through to the ordinary name search, so a player who
// upgrades, or who drops a WAD into the folder by hand, notices nothing. Adoption into the store is
// opportunistic, never required.
//
// WE ONLY EVER TOUCH OUR OWN DOWNLOAD FOLDER. Files the player keeps elsewhere are read and never
// moved, renamed or deleted. A feature that reorganised somebody's WAD collection to suit itself
// would be a disaster, however tidy the result.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_WADSTORE_COMPUTE_H
#define ZX_WADSTORE_COMPUTE_H

#include <cstddef>
#include <string>
#include <vector>

namespace zx
{

// The subdirectory of the download folder that holds the content-addressed store. Loose files sit
// beside it, not inside it, so "delete this one folder" remains a complete uninstall.
extern const char *const kStoreDirName;

// Whether `text` is exactly `expectedLen` hex digits. Case-insensitive. Digests arrive from a remote
// server, and this one gets pasted into a filesystem path -- so it is checked before it is used,
// rather than trusted because it usually looks right.
bool IsHexDigest(const std::string &text, size_t expectedLen);

// Path of a stored file relative to the download folder: "by-hash/<lowercase digest>/<name>".
// Empty when the digest is not a plausible hex digest of `digestLen`, or the name is not a bare
// filename we would create -- and empty must be read as "do not store this", never as a valid path.
std::string StoredRelativePath(const std::string &digestHex, const std::string &name,
	size_t digestLen);

// Just the directory half, for creating it before the write.
std::string StoredRelativeDir(const std::string &digestHex, size_t digestLen);

// NOTE ON TRUST. A store hit is taken on the strength of its path: the digest names the folder, so
// the file in it is that content unless something edited it in place. We do not re-hash to confirm,
// because avoiding exactly that read is the point of the store, and the folder is ours -- nothing
// but this code is supposed to write there. The cheap confirmation would be a size check, and that
// becomes possible once the server publishes sizes (SQF2_WAD_SIZES); until then there is nothing
// honest to compare against, so no check is claimed.

// One file in the store, for pruning.
struct StoreEntry
{
	long long sizeBytes;
	long long lastUsedMs;		// monotonic; larger is more recent

	StoreEntry() : sizeBytes(0), lastUsedMs(0) {}
	StoreEntry(long long size, long long used) : sizeBytes(size), lastUsedMs(used) {}
};

// Indices of the entries to delete to bring the store within `capBytes`, least recently used first.
// Empty when it already fits, or when `capBytes` <= 0, which means no cap.
//
// Versions accumulate here instead of overwriting each other, which is the whole point and also the
// whole cost -- so something has to bound it, and "oldest unused build goes first" is the rule that
// keeps what a player is actually playing.
std::vector<size_t> ComputePruneOrder(const std::vector<StoreEntry> &entries, long long capBytes);

} // namespace zx

#endif // ZX_WADSTORE_COMPUTE_H
