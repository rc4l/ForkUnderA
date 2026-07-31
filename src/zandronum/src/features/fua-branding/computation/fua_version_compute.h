// [rc4l] Turns the build's `git describe` string into the pieces the console line and window title
// show: our version tag, and whether this build is a stable release or an experimental one.
//
// The release channel is not a separate build flag on purpose. `git describe` already encodes it:
// a build made exactly at a tag describes as "v0.1.19", while any commit after that tag describes
// as "v0.1.19-29-gde55d35" (tag, commits-since, short hash). So "has a -<N>-g<hash> suffix" IS
// "not a tagged release", with nothing extra to wire up and nothing that can drift out of sync
// with reality -- a flag passed by CI could be set wrong; the describe string cannot.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_FUA_VERSION_COMPUTE_H
#define ZX_FUA_VERSION_COMPUTE_H

#include <cstddef>

namespace zx
{

// [rc4l] True when `describe` names a tag exactly, i.e. this build IS a released version rather
// than some commit after one. An empty or unrecognisable string is treated as NOT stable: claiming
// "stable" is the dangerous direction to be wrong in, so ambiguity resolves to experimental.
bool FuaIsStableBuild(const char *describe);

// [rc4l] Copies just the version tag out of `describe` ("v0.1.19-29-gde55d35" -> "v0.1.19") into
// `out`, always NUL-terminated. When there is no recognisable tag the output is the whole input
// (truncated to fit), so the line still shows something useful rather than going blank on a build
// made from a shallow clone with no tags.
void FuaVersionTag(const char *describe, char *out, size_t outSize);

} // namespace zx

#endif // ZX_FUA_VERSION_COMPUTE_H
