// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Starting a hosted game from the console, so hosting can be driven without the menus.
//
// The menus are the only way to reach HostStart today, which makes the hosting path awkward to
// exercise from a script and impossible to assert on: an end-to-end check of "leave a hosted game
// and get it back" has to be able to start one in the first place.
//
// The parsing is here rather than in the command because it is the part worth being sure about. A
// map name and a server name both come from whoever typed the line, and every value is checked with
// hostargs_compute's own rules -- the same ones the menu path uses -- so a map called `-host` or a
// server name carrying `+exec` is refused rather than escaped.
//
// Header-pure by the features/ rules: no engine types.

#ifndef ZX_HOSTCOMMAND_COMPUTE_H
#define ZX_HOSTCOMMAND_COMPUTE_H

#include "features/server-hosting/computation/hostargs_compute.h"

#include <string>
#include <vector>

namespace zx
{

// Turn `fua_host <map> [name <server name>] [port <n>] [players <n>] [file <wad>]...` into a config.
//
// False with a reason in `error` for anything that does not add up. The IWAD and the loaded WADs are
// NOT taken from the line -- the caller fills those in from what is already running, because hosting
// a set you are not holding is a different feature.
bool ParseHostCommand(const std::vector<std::string> &args, HostConfig &out, std::string &error);

} // namespace zx

#endif // ZX_HOSTCOMMAND_COMPUTE_H
