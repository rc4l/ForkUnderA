// [rc4l] Pure, engine-free decision logic for in-engine WAD reloading (issue #82, Phase 1). No
// engine headers -- only the standard library -- so it is unit-tested off-engine and the coverage
// gate enforces 100% on the matching *_compute.cpp.
//
// The engine already has ZDoom's restart cycle: D_DoomMain re-reads the WAD set from the global
// Args every loop iteration, so a reload is "rewrite Args, throw CRestartException, come back up on
// the new set". The two fiddly, bug-prone pieces are decided here so they can be tested without the
// engine: (1) does the wanted set already match what's loaded (skip the restart), and (2) rewriting
// the argv token list to drop the old -iwad/-file and append the new set. DArgs::RemoveArgs gets (2)
// wrong at the array tail (it leaves the last value behind), which is exactly why we own it here.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_WADRELOAD_COMPUTE_H
#define ZX_WADRELOAD_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace wadreload {

// Canonicalize a WAD path's *representation* for set comparison: lowercase, backslashes -> forward
// slashes, and trim surrounding whitespace. It deliberately does NOT resolve relative paths or
// symlinks (the engine layer passes already-absolute paths) -- it only makes "A.WAD" and "a.wad"
// compare equal so a case difference doesn't force a pointless reload.
std::string NormalizePath(const std::string &p);

// True when the wanted WAD set equals the currently loaded one: same IWAD and the same PWAD list in
// the same order. Order matters -- Doom load order controls which lump wins -- so a reordering is a
// real change and returns false. All entries compared through NormalizePath. When this is true the
// caller skips the restart entirely (connecting to a server whose WADs you already have shouldn't
// pay a full re-init).
bool WantedMatchesLoaded(const std::string &curIwad, const std::vector<std::string> &curFiles,
                         const std::string &wantIwad, const std::vector<std::string> &wantFiles);

// A token counts as a switch (not a value) when it is non-empty and begins with '-' or '+'. Empty
// tokens and anything else are values. Exposed for testing the tail behavior.
bool IsSwitchToken(const std::string &tok);

// Rewrite an argv token list for a reload: drop each switch named in `removeSwitches` together with
// its trailing values (every following token up to the next switch), keep every other token AND
// argv[0] (the program name), then append `appendTokens` verbatim at the end. Correct regardless of
// where the removed switch sits -- including at the very tail, where DArgs::RemoveArgs mishandles the
// last value. The engine feeds appendTokens = {"-iwad", <iwad>, "-file", <pwad1>, <pwad2>, ...}.
std::vector<std::string> ComputeReloadArgv(const std::vector<std::string> &argv,
                                           const std::vector<std::string> &removeSwitches,
                                           const std::vector<std::string> &appendTokens);

// The engine's FResourceFile accepts ANY existing file (its format table ends in a catch-all that
// wraps a single unrecognized file as one lump), so it can't tell a real WAD from a truncated/garbage
// download. For a reload/download safety gate we want the opposite: accept only recognized archive
// containers. ClassifyArchiveMagic inspects the leading bytes and reports which container it is, or
// Unknown -- the engine reads a file's first bytes and refuses the reload when this is Unknown.
enum class ArchiveKind { Unknown, Wad, Zip, SevenZip };
ArchiveKind ClassifyArchiveMagic(const unsigned char *bytes, size_t n);

// If `token` is a "map=<NAME>" assignment (case-insensitive key, à la Odamex's "lastmap="), return
// <NAME> verbatim; otherwise return "". An empty value ("map=") returns "" (treated as no map). Lets
// wad_reload take an optional "map=MAP02" to boot straight into a map after the reload, kept
// unambiguous from wad paths (which never start with "map=").
std::string ParseMapAssignment(const std::string &token);

}} // namespace zx::wadreload

#endif // ZX_WADRELOAD_COMPUTE_H
