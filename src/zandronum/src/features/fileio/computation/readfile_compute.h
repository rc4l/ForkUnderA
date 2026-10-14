// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A file read whole into memory, for the features that each kept their own copy of this loop.

#ifndef ZX_READFILE_COMPUTE_H
#define ZX_READFILE_COMPUTE_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace zx {

// [rc4l] An open file's bytes from where it stands to its end into out, read in chunks so its size need not be known; false when a read fails or it holds more than maxBytes.
bool ComputeReadWhole(FILE *file, std::string &out, size_t maxBytes = SIZE_MAX);

// [rc4l] The same for the file at path, false too when it cannot be opened.
bool ComputeReadWholeFile(const char *path, std::string &out, size_t maxBytes = SIZE_MAX);

// [rc4l] The file's bytes, or none when it cannot be read, for a caller to whom a missing file and an empty one mean the same.
std::string ComputeFileText(const char *path);

} // namespace zx

#endif // ZX_READFILE_COMPUTE_H
