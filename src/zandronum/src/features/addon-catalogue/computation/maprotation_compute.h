// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The maps a server.cfg puts in its rotation, read out of the cfg itself.
//
// The cfg is the server's file and this client has never parsed one: it is handed over with +exec
// and what it says is the server's business. This is the one exception, and it is a narrow one --
// only `addmap` lines, only to name them, and never to act on anything else in the file.
//
// The alternative was to write the rotation into addon.json a second time so the panel could see it,
// which is the copies-drift problem the schema refuses everywhere else. Two lists of thirty-two map
// names, edited by hand, in two files that nothing checks against each other: the day they disagree
// the panel offers a starting map the server will not go to, and nothing says so.
//
// Deliberately not a cfg parser. It does not know what `sv_nojump` is, it does not follow `exec`,
// and a line it cannot read is a line it skips. The worst a strange file can do is yield no maps,
// which reads as "this pack does not write a rotation" -- which is true of several of them.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_MAPROTATION_COMPUTE_H
#define ZX_MAPROTATION_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// In the order written. Duplicates are dropped, keeping the first: a rotation that names a map twice
// is still one place to start from, and a picker offering it twice would have two stops that do the
// same thing.
//
// The command is matched case-insensitively, because the cfgs in this catalogue already spell it
// both ways -- Ghouls vs Humans writes `addmap Gvh00` and everything else writes lower case -- and a
// reader that cared would silently return nothing for one of them.
std::vector<std::string> MapsInRotation(const std::string &cfgText);

} // namespace zx

#endif // ZX_MAPROTATION_COMPUTE_H
