// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The page's URL query as the engine's command line, so a link can name the game, the wads and the map.

#ifndef ZX_WEBARGS_COMPUTE_H
#define ZX_WEBARGS_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace web {

// [rc4l] Arguments after the program name from a query such as "?iwad=freedoom2.wad&file=a.pk3,b.pk3&map=MAP01&skill=3&args=-nomonsters":
// iwad, file (comma separated), map (+map), skill and warp become their flags, args is split on spaces as typed, and unknown keys are dropped.
std::vector<std::string> ComputeArgsFromQuery(const std::string &query);

// [rc4l] A query value decoded: %XX to its byte and '+' to a space, as a form encodes them; a malformed escape is kept as it was.
std::string ComputeDecodeQueryValue(const std::string &value);

}} // namespace zx::web

#endif // ZX_WEBARGS_COMPUTE_H
