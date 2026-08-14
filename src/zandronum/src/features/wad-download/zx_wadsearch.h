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

#include "tarray.h"
#include "zstring.h"

namespace zx
{

// Full path to the IWAD named `name`, or "" if nothing has it. Searches everything the engine's own
// IWAD detection searches: IWADSearch.Directories from the config, then every Steam library
// I_GetSteamPath reports. Callers should try D_AddFile FIRST -- this is the widening step, not a
// replacement for the ordinary file search.
FString FindIwadInEngineSearchPaths( const char *name );

// [rc4l] Full path to the PWAD/PK3 named `name`, or "" if this machine does not have it. Walks what
// BaseFileSearch walks for a -file argument: the path as given, then FileSearch.Directories.
//
// Exists because the obvious way to ask this question is wrong. D_AddFile's third parameter is
// `check`, and passing false does not mean "check quietly" -- it means DO NOT CHECK, so the call
// returns true for a file that is not there. Two callers asked it that way, which is why hosting an
// entry whose files were missing started a server anyway and only wad_reload found out.
FString FindFileInEngineSearchPaths( const char *name );

// [rc4l] Every copy of `name` in those same places, in search order, rather than just the first.
// The PWAD counterpart of FindAllIwadsInEngineSearchPaths, and for the same reason: one name is not
// one file. Which of them is meant is a question only a digest can answer, so the caller is given
// all of them to choose from instead of whichever the search order reached first.
void FindAllFilesInEngineSearchPaths( const char *name, TArray<FString> &out );

// [rc4l] Bytes on disk, or 0 if it is not there. One stat, no reading -- which is what makes it
// affordable to ask about every file of an entry while drawing.
unsigned long long FileSizeOnDisk( const char *path );

// [rc4l] Every copy of `name` in those same places, in search order, rather than just the first.
//
// One name is not one file. doom2.wad shipped as 1.666, 1.7, 1.8, 1.9, a French build, BFG Edition
// and the 2024 KEX re-release, and a player can easily have two of them -- one from Steam, one in
// their own WAD folder. Taking the first match means loading whichever the search order happened to
// reach, and if that is not the build the server runs, level authentication rejects the join and
// says nothing about why. Collecting them all lets the caller pick by digest.
//
// Read-only: nothing here is moved, renamed or deleted. A player's WAD collection is theirs.
void FindAllIwadsInEngineSearchPaths( const char *name, TArray<FString> &out );

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
