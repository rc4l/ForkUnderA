// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Fua IWAD registration: where a copy of an IWAD lives once we have taken one.
//
// One store per user, shared by every ForkUnderA on the machine, so a second download of the engine
// does not mean a second copy of everybody's IWADs:
//
//     <data root>/ForkUnderA/core/iwads/<sha256>/<filename>
//
// Addressed by CONTENT rather than by name, because names lie. The same file arrives as DOOM2.WAD,
// doom2.wad and doom2 (1).wad, and two genuinely different builds arrive as the same name, which
// is the whole reason the allowlist already keys on the digest. Filed under its hash, one file is
// one entry and two builds are two, whatever anyone called them.
//
// The filename is still kept as the leaf, because the engine identifies IWADs partly by name and a
// store full of extensionless hashes would be unusable by anything but us.
//
// Header-pure by the features/ rules: this decides PATHS and says nothing about the filesystem. The
// caller does the hashing, the copying and the asking-the-OS-where-the-data-root-is.

#ifndef ZX_IWADREGISTRY_COMPUTE_H
#define ZX_IWADREGISTRY_COMPUTE_H

#include <string>

namespace zx
{

// A SHA-256 digest as it must appear in a path: lower case, hex, exactly 64 characters.
//
// Lower case is not cosmetic. Windows folds case in paths and Linux does not, so the same digest
// written both ways is ONE folder on Windows and TWO on Linux, a split store that only shows up on
// the platform it is hardest to notice on. Normalised in one place so it cannot be got wrong twice.
//
// Returns "" for anything that is not a 64-character hex string, which is how a caller whose hash
// failed says so without inventing a folder to put the file in.
std::string NormalizeDigest(const std::string &sha256Hex);

// The folder a digest's copy belongs in, under `root` (the ForkUnderA data root, already resolved
// by the caller from the OS). No trailing slash. Empty when the digest is unusable.
std::string IwadStoreDir(const std::string &root, const std::string &sha256Hex);

// The full path of the stored copy, including the leaf name. Empty when the digest is unusable or
// the name is one we will not write.
std::string IwadStorePath(const std::string &root, const std::string &sha256Hex,
                          const std::string &fileName);

// Whether `fileName` is safe to use as the leaf of a store path.
//
// The name reaching here came off a wire or a disk somewhere and is not ours. A name carrying a
// separator or a parent reference would place the copy OUTSIDE its digest folder, which is the one
// way a content-addressed store can be made to overwrite something.
bool IsSafeStoreName(const std::string &fileName);

} // namespace zx

#endif // ZX_IWADREGISTRY_COMPUTE_H
