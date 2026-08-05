// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Finding an IWAD the way the ENGINE finds one, and teaching the engine about the folders
// other Doom tools use.
//
// Both of these exist because of the same mistake. The join path resolved the server's IWAD with
// D_AddFile, which goes through BaseFileSearch -- and BaseFileSearch reads FileSearch.Directories,
// the -file search path. The engine's own IWAD detection (FIWadManager::IdentifyVersion) reads
// IWADSearch.Directories AND scans I_GetSteamPath(). Those are different lists:
//
//   FileSearch.Directories   Windows: $PROGDIR, $DOOMWADDIR
//   IWADSearch.Directories   Windows: ".", $DOOMWADDIR, $HOME, $PROGDIR   + every Steam library
//
// So a player who bought Doom II on Steam joined a doom2.wad server, we failed to find the IWAD the
// engine finds at startup without trouble, and substituted Freedoom -- telling someone who owns the
// game that they do not. Substitution is supposed to be the last resort, and it cannot be that if we
// give up before looking where the engine looks.

#ifndef ZX_WADSEARCH_H
#define ZX_WADSEARCH_H

#include "zstring.h"

namespace zx
{

// Full path to the IWAD named `name`, or "" if nothing has it. Searches everything the engine's own
// IWAD detection searches: IWADSearch.Directories from the config, then every Steam library
// I_GetSteamPath reports. Callers should try D_AddFile FIRST -- this is the widening step, not a
// replacement for the ordinary file search.
FString FindIwadInEngineSearchPaths( const char *name );

// Add the well-known WAD folders belonging to other Doom tools (waddirectories.txt, compiled in) to
// the config's FileSearch.Directories and IWADSearch.Directories. Only ones that actually exist, only
// once each -- it is safe to call repeatedly and cheap when there is nothing to do.
//
// Registering into the config rather than consulting the list privately is deliberate: a player who
// already downloaded a mod through Doomseeker should not re-download it for ANY purpose, not just for
// a join, and the added paths are visible in the ini where they can be removed.
void RegisterKnownWadDirectories( void );

} // namespace zx

#endif // ZX_WADSEARCH_H
