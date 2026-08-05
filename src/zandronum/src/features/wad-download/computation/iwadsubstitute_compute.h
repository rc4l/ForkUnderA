// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What to load when a server's IWAD is a game the player does not own.
//
// Freedoom is a from-scratch, BSD-licensed replacement for Doom's data. A client with Freedoom can
// load a Doom II mod's resources without owning Doom II, which is why Zandronum's own wiki says
// Freedoom "will allow users to connect to almost any server plausible". So when a server wants
// doom2.wad and the player has none, the honest options are "refuse the join" or "load Freedoom and
// say so" -- and the second is right far more often than it sounds, because most of the Zandronum
// browser is servers running PWADs that replace every map, where the IWAD supplies only textures,
// sounds and actor definitions.
//
// Two things this is careful about:
//
//   1. It is a FALLBACK, never a preference. The caller resolves the server's real IWAD first and
//      only asks here when that came up empty. Owning doom2.wad always means loading doom2.wad.
//   2. It is not a fix for stock maps. Freedoom's MAP01 is not Doom II's MAP01, so on a server
//      running stock levels the substituted client loads different geometry and Zandronum's level
//      authentication rejects it. The engine says which IWAD it substituted, and there is a CVAR to
//      turn it off, because the player is the one who can tell which case they are in.
//
// The table is the repo-root iwadsubstitutes.txt, compiled in by tools/gen-wadlists.cmake -- which
// also fails the build if a replacement is not in iwadallowlist.txt, since a substitute we are not
// allowed to download is no use as a stand-in for a game we are not allowed to download.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_IWADSUBSTITUTE_COMPUTE_H
#define ZX_IWADSUBSTITUTE_COMPUTE_H

#include <string>

namespace zx
{

// The free IWAD that can stand in for `wantedIwadName`, or "" if there is none. Case-insensitive;
// `wantedIwadName` is a bare filename as the server spelled it. The returned name is always one the
// download gate will accept, so a caller may hand it straight to the downloader.
std::string FreeIwadSubstituteFor(const std::string &wantedIwadName);

} // namespace zx

#endif // ZX_IWADSUBSTITUTE_COMPUTE_H
