// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which files we are willing to download, and specifically: never a game someone sells.
//
// A PWAD is a mod. An IWAD is the game -- and for Doom, Doom II, Final Doom, Heretic, Hexen and
// Strife the game is something a player buys. Downloading one is redistributing it, so the engine has
// to refuse, and refuse for the right reason: not because we recognised a filename, but because we
// could not confirm it was free to redistribute.
//
//   IWADs are deny-by-default. Every IWAD is assumed to be commercial. One is downloaded only if its
//   name is on the allowlist below of IWADs known to be freely redistributable. Unknown means no.
//
// That direction is the whole design, and it is where this differs from Odamex, which keeps a
// DENYLIST of commercial files (common/w_ident.cpp: W_IsFilenameCommercialWAD, and an MD5 table).
// A denylist can only ever describe games that already existed when it was written, so the
// commercial IWAD released next year downloads happily -- and keeping it accurate makes us
// responsible for tracking every game in the ecosystem forever. Assuming the worst and listing only
// the exceptions is both safer and far less to maintain: we never have to know that doom2.wad is
// sold, only that freedoom2.wad is not.
//
// Two gates implement it, because a name is a claim and the claim comes from the server:
//
//   1. Before fetching: a file the server declares as its IWAD must be on the allowlist.
//   2. After the bytes land: a file whose header says IWAD must ALSO be on the allowlist, whatever it
//      was called and whichever slot asked for it. This is the one that actually holds -- it catches
//      doom2.wad renamed to coolmod.wad and listed as a PWAD, which no name-based rule can, and it
//      is why gate 1 does not need a list of games to refuse. Odamex reaches the same place by MD5:
//      exact, but only for the hashes they enumerated, so a fresh release or a differently-patched
//      copy walks past it. Reading the file's own header needs no table and does not go stale.
//
// THE ALLOWLIST IS NOT CONFIGURABLE, and that is the point. An earlier draft had a CVAR to extend it,
// reasoning that free IWADs keep being made and players should not wait on us shipping a build. But a
// list anyone can append to is not a gate: the first thing written to it would be "doom2.wad", by a
// server operator's setup guide, in a config a player pastes without reading. The whole design rests
// on the list being something we vouched for, so it lives in the binary and adding to it is a code
// change with a commit behind it -- which is exactly the amount of friction a legal assertion should
// have.
//
// Header-pure by the features/ rules -- no engine types.

#ifndef ZX_IWADALLOW_COMPUTE_H
#define ZX_IWADALLOW_COMPUTE_H

#include <cstddef>
#include <string>

namespace zx
{

enum class DownloadVerdict
{
	Allowed,
	UnsafeName,				// not a bare filename, or not a resource type the engine loads
	UnlistedIwad,			// an IWAD we cannot confirm is free to redistribute -- assume it is sold
};

// A short human sentence for the verdict, for the console and the "can't join" message. Never NULL.
const char *DownloadVerdictReason(DownloadVerdict verdict);

// Whether `name` is one of the built-in IWADs known to be freely redistributable. Case-insensitive;
// `name` is a bare filename.
bool IsFreeIwadName(const std::string &name);

// Whether the first bytes of a file are a Doom IWAD directory header. `len` may be short; a file too
// small to have a header is not an IWAD.
bool HeaderIsIwadMagic(const char *header, size_t len);

// Verdict on a file BEFORE fetching it. `isIwadSlot` is true when the server declared this file as
// its IWAD rather than as a PWAD. PWADs pass this gate on name alone -- mods are the ordinary case,
// and what a PWAD actually turns out to be is settled by ClassifyDownloadedFile.
DownloadVerdict ClassifyWantedFile(const std::string &name, bool isIwadSlot);

// Verdict on a file AFTER fetching it, given the bytes that arrived. Re-runs the name check (the name
// did not become safer) and adds the one that needs the file itself. Callers must delete the file on
// anything but Allowed.
DownloadVerdict ClassifyDownloadedFile(const std::string &name, const char *header, size_t len);

} // namespace zx

#endif // ZX_IWADALLOW_COMPUTE_H
