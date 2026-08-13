// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Fua IWAD registration, meaning where a copy of an IWAD lives once we have taken one.
//
// One store per user, shared by every ForkUnderA on the machine:
//
//     <data root>/ForkUnderA/core/iwads/<sha256>/<filename>
//
// Addressed by CONTENT rather than by name, because names lie in both directions and a digest does
// not.
//
// The filename is still kept as the leaf, because the engine identifies IWADs partly by name.
//
// Header-pure by the features/ rules, so this decides paths and the caller does the hashing, the
// copying and the asking the OS where the data root is.

#ifndef ZX_IWADREGISTRY_COMPUTE_H
#define ZX_IWADREGISTRY_COMPUTE_H

#include <string>

namespace zx
{

// A SHA-256 digest as it must appear in a path, being lower case hex of exactly 64 characters.
//
// Lower case is not cosmetic, since Windows folds case in paths and Linux does not, so a digest
// written both ways would be one folder on Windows and two on Linux.
//
// Returns "" for anything that is not a 64-character hex string, which is how a caller whose hash
// failed says so without inventing a folder to put the file in.
std::string NormalizeDigest(const std::string &sha256Hex);

// The folder a digest's copy belongs in under `root`, with no trailing slash, and empty when the
// digest is unusable.
std::string IwadStoreDir(const std::string &root, const std::string &sha256Hex);

// The full path of the stored copy, empty when the digest is unusable or the name is one we will
// not write.
std::string IwadStorePath(const std::string &root, const std::string &sha256Hex,
                          const std::string &fileName);

// Whether `fileName` is safe to use as the leaf of a store path, a name carrying a separator or a
// parent reference being the one way a content-addressed store can be made to overwrite something.
bool IsSafeStoreName(const std::string &fileName);

} // namespace zx

#endif // ZX_IWADREGISTRY_COMPUTE_H
