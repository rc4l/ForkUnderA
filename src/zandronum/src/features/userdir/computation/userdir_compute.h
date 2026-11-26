// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Where the per-user folder (identity keys, Continue records, the IWAD and PWAD stores) lives: beside the node cache, or where -fua_userdir puts it.

#ifndef ZX_USERDIR_COMPUTE_H
#define ZX_USERDIR_COMPUTE_H

#include <string>

namespace zx { namespace userdir {

// [Doruk-Aksoy] The given folder when there is one (forward slashes, no trailing slash), else cachePath less its last two parts ("/zdoom/cache") with gameName after it.
std::string ComputeFuaUserPath(const char *givenPath, const char *cachePath, const char *gameName);

}} // namespace zx::userdir

#endif // ZX_USERDIR_COMPUTE_H
