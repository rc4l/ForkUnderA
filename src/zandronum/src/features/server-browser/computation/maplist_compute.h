// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Which lumps of a file are maps, and what the rotation across several files comes to.
//
// The NEW screen has to list the maps of files it has NOT loaded -- the player is choosing what a
// server will run, and the client is not running it. So the caller opens each file and hands over
// its directory, and this decides what in there is a map.
//
// TWO RULES, because there are two kinds of file:
//
//   A WAD names a map with an ordinary lump and follows it with the map's own data. The marker is
//   the FIRST of those: THINGS for a Doom-format map, TEXTMAP for UDMF. So a lump whose next lump
//   is one of those two is a map header, and its name is the map's name. Nothing else identifies
//   it -- a map header lump is empty and can be called anything.
//
//   A PK3 keeps each map as its own file under maps/, so maps/MAP01.wad is the map MAP01. The
//   entry has a path rather than an eight-character name, which is why the caller passes both.
//
// Both rules run over the same list, because a pk3 may hold loose lumps as well.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_MAPLIST_COMPUTE_H
#define ZX_MAPLIST_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// One entry of a file's directory, in the order the file lists it.
struct LumpEntry
{
	std::string name;		// the eight-character name, upper case as the engine reports it
	std::string path;		// the full path inside an archive, empty for a plain WAD lump

	LumpEntry() {}
	LumpEntry(const std::string &n, const std::string &p) : name(n), path(p) {}
};

// The maps this file holds, in the order it holds them.
std::vector<std::string> MapsInFile(const std::vector<LumpEntry> &lumps);

// [rc4l] `incoming` added to `into`, in order, skipping what is already there.
//
// A pwad that replaces MAP01 does not add a second MAP01 to the rotation: the server would visit
// the same map twice and the second visit would be the same map. The FIRST position is kept, so a
// rotation reads in the order the files were loaded.
void MergeMaps(std::vector<std::string> &into, const std::vector<std::string> &incoming);

// Whether a name can be a map lump at all: one to eight characters, letters, digits, underscore.
// Anything else is a lump this has misread, and putting it on a command line would be worse than
// dropping it.
bool IsMapName(const std::string &name);

} // namespace zx

#endif // ZX_MAPLIST_COMPUTE_H
