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

// [rc4l] The arguments a page reloaded by a restart starts the engine with: those it held after the program name, and with a wad_reload's map, that map in place of any +map.
std::vector<std::string> ComputeRestartArgs(const std::vector<std::string> &held, const std::string &map);

// [rc4l] Arguments as a JSON array of strings, as the page keeps them across the reload.
std::string ComputeArgsJson(const std::vector<std::string> &args);

}} // namespace zx::web

#endif // ZX_WEBARGS_COMPUTE_H
