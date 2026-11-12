// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How BuildString writes one argument back into the command line it could have come from.

#ifndef ZX_CMDLINEARG_COMPUTE_H
#define ZX_CMDLINEARG_COMPUTE_H

#include <string>

namespace zx {

// [rc4l] An argument as a console command line reads it back whole: an empty one as "", one with quotes escaped inside quotes, one with a space or a semicolon quoted, any other as it is.
std::string ComputeQuotedArgument(const std::string &arg);

} // namespace zx

#endif
