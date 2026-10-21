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

// [rc4l] Where a rotation should BEGIN, given the map that was asked for.
//
// The server ignores the map on its command line whenever a rotation exists: MAPROTATION_StartNewGame
// takes position 0 (or a random one) and hands that to G_InitNew, so +map is dropped on the floor for
// every experience in this catalogue that writes a rotation. Starting a rotation AT a map is what
// "first map" has to mean anyway -- the alternative, opening on one map and then continuing from the
// top of the list, is not something anybody would ask for.
//
// Matched case-insensitively, because a rotation is written by hand and a map name is a lump name:
// the catalogue already contains `addmap Gvh00` and `addmap D2CTF1` in the same breath.
//
// A name the rotation does not hold gives 0, which is where it would have started. An unmet request
// is not worth refusing a server over, and there is nowhere to say so by the time this is asked.
size_t MapRotationStart(const std::vector<std::string> &maps, const std::string &wanted);

} // namespace zx

#endif // ZX_MAPROTATION_COMPUTE_H
