// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Cvars known by a fua_ name as well as their own: the alias table and its lookup.

#ifndef ZX_CVARALIAS_COMPUTE_H
#define ZX_CVARALIAS_COMPUTE_H

#include <stddef.h>

namespace zx { namespace cvaralias {

// [rc4l] The cvar that `name` (its first `len` characters, compared without case) stands for, or NULL when it is no alias.
const char *ComputeCVarAliasTarget(const char *name, size_t len);

}} // namespace zx::cvaralias

#endif // ZX_CVARALIAS_COMPUTE_H
