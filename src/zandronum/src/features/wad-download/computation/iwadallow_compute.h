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
// Two gates implement it, and only the second is a security boundary:
//
//   1. Before fetching (name): a file the server declares as its IWAD must be on the name allowlist.
//      This is an EARLY-OUT, not the gate. It exists so we do not pull 40 MB of something we would
//      only delete -- and because transiently writing a commercial IWAD to disk is still downloading
//      it. A filename is a claim made by the server, so it can never be the thing we rely on.
//   2. After the bytes land (hash): a file that is going to be loaded AS a game is kept only if its
//      SHA-256 is one we shipped. This is the gate. It is the only check a rename cannot walk past --
//      doom2.wad served under the name freedoom2.wad passes every name check ever written and fails
//      this one.
//
// "Going to be loaded as a game" is deliberately NOT just "has IWAD magic", and that cost us a bug.
// Chex Quest ships chex.wad and chex3.wad with PWAD magic; the engine loads them as IWADs anyway,
// by matching lumps out of wadsrc/static/iwadinfo.txt. So a magic-only test skips the hash check on
// exactly the files an allowlist entry was written for. The rule is therefore: the file is gated if
// the server declared it as its IWAD, OR its header says IWAD whatever slot asked for it. The first
// catches Chex; the second catches doom2.wad smuggled in as a "PWAD".
//
// It still cannot be "everything must be in the hash list", because that would refuse every PWAD ever
// made: we can enumerate free IWADs, we cannot enumerate mods.
//
// Odamex reaches the same place from the opposite direction -- an MD5 DENYlist of commercial files.
// That works for them because the commercial set they enumerate stopped growing; it does not work
// now, because doom2.wad alone has nine-odd released builds and Doom-engine games are still being
// sold. It also lets them use MD5, where we cannot: a collision against a denylist merely refuses
// something harmless, while a collision against an allowlist is the whole gate falling open, and
// chosen-prefix MD5 collisions are practical. Hence SHA-256.
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
	UnvouchedIwadBuild,		// the right filename, but bytes no shipped hash vouches for
};

// A short human sentence for the verdict, for the console and the "can't join" message. Never NULL.
const char *DownloadVerdictReason(DownloadVerdict verdict);

// Whether `name` is one of the built-in IWADs known to be freely redistributable. Case-insensitive;
// `name` is a bare filename.
bool IsFreeIwadName(const std::string &name);

// Whether the first bytes of a file are a Doom IWAD directory header. `len` may be short; a file too
// small to have a header is not an IWAD.
bool HeaderIsIwadMagic(const char *header, size_t len);

// Whether `sha256Hex` is a build we shipped a hash for, AND that build is the file `name` claims to
// be. Case-insensitive on both. The name half is not a security check -- both sides are already free
// IWADs -- it stops a mirror serving Freedoom Phase 1 as freedoom2.wad and poisoning the cache with a
// file that will never load the maps its name promises.
bool IsVouchedIwadBuild(const std::string &sha256Hex, const std::string &name);

// Verdict on a file BEFORE fetching it. `isIwadSlot` is true when the server declared this file as
// its IWAD rather than as a PWAD. PWADs pass on name alone -- mods are the ordinary case, and what a
// PWAD actually turns out to be is settled by ClassifyDownloadedFile.
DownloadVerdict ClassifyWantedFile(const std::string &name, bool isIwadSlot);

// Verdict on a file AFTER fetching it. `isIwadSlot` is the same flag ClassifyWantedFile was given --
// it has to be re-supplied because a file can be destined to load as a game without saying so in its
// header (Chex Quest). `sha256Hex` is the digest of what actually arrived; pass "" when it could not
// be computed, which is treated as "cannot vouch" rather than "fine". Callers must delete the file on
// anything but Allowed.
DownloadVerdict ClassifyDownloadedFile(const std::string &name, bool isIwadSlot, const char *header,
	size_t len, const std::string &sha256Hex);

} // namespace zx

#endif // ZX_IWADALLOW_COMPUTE_H
