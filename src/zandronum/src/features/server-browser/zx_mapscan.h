// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The maps inside a file this client has NOT loaded.
//
// The NEW screen builds a server out of files sitting on disk; nothing about them is in the wad
// tables, because the client is not running what it is configuring. So the file is opened on its
// own, its directory read, and closed again -- FResourceFile is the engine's own reader for every
// archive it supports, so a pk3, a zip, a pk7 and a plain wad all answer the same question without
// a format reader written here.
//
// Only the DIRECTORY is read. Opening a hundred-megabyte pk3 costs its central directory and
// nothing else, which is what makes this affordable to do for a whole load order.
//
// The deciding is maplist_compute's; this only fetches.

#ifndef ZX_MAPSCAN_H
#define ZX_MAPSCAN_H

#include <string>
#include <vector>

namespace zx
{

// Every map in `path`, in file order. Empty when the file cannot be opened or holds none.
std::vector<std::string> MapsInPath(const std::string &path);

} // namespace zx

#endif // ZX_MAPSCAN_H
