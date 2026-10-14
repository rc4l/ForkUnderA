// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] How the line-finding console commands (fua_find_lines) read their arguments and match textures, engine-free.

#ifndef ZX_LINEQUERY_COMPUTE_H
#define ZX_LINEQUERY_COMPUTE_H

namespace zx { namespace levelmesh {

// [rc4l] The text after "key=" when arg is that key's argument, else nullptr.
const char *ComputeKeyArgValue(const char *arg, const char *key);

// [rc4l] Whether name starts with prefix, ignoring case as texture names do, so WFALL finds every frame of a waterfall.
bool ComputeHasNamePrefix(const char *name, const char *prefix);

}} // namespace zx::levelmesh

#endif
