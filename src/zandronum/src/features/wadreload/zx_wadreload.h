// [rc4l] In-engine WAD reloading (issue #82, Phase 1). Drives ZDoom's existing restart cycle from
// code: validate a wanted WAD set, and if it differs from what's loaded and every file is loadable,
// rewrite the global Args and throw CRestartException so D_DoomMain comes back up on the new set.
//
// Rollback-by-not-committing: because I_FatalError on this engine calls exit() (it does not throw),
// there is no way to catch a bad WAD *during* re-init and recover. So instead we NEVER tear the
// running game down unless the new set is provably loadable -- we validate first (mirroring
// FWadCollection::AddFile's acceptance test) and refuse the reload otherwise, leaving the current
// game exactly as it was. That is a stronger guarantee than fail-then-reload-the-old-set.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_WADRELOAD_H
#define ZX_WADRELOAD_H

#include "tarray.h"
#include "zstring.h"

namespace zx { namespace wadreload {

enum class ReloadResult
{
	Restarting,      // never actually returned -- the call throws CRestartException on this path
	AlreadyLoaded,   // wanted set already matches the loaded set; nothing done
	InvalidWads,     // one or more wanted files aren't loadable resources; nothing done (rollback)
};

// True if `path` is safe to reload onto: an existing directory mod, or a file whose leading bytes are
// a recognized ARCHIVE magic (WAD/PK3/PK7). This is deliberately STRICTER than FWadCollection::AddFile,
// which wraps any unrecognized file as a single lump and so would silently accept a truncated/garbage
// download -- exactly what a reload/download gate must refuse. On failure sets `outWhy` to a short
// human reason. (Deep integrity / hash verification is the later download phase's job.)
bool WadLoadable(const char *path, FString &outWhy);

// [rc4l] Clear the startup state that is written once-if-empty and so survives a restart onto a
// DIFFERENT WAD set. Call once at the top of D_DoomMain's reinit loop, restart path only.
//
// This lives here because reloading is what exposes it: the state is harmless while a restart means
// "the player asked to change WADs and expects a fresh engine", and wrong once wad_reload makes
// restarting routine -- joining a server from the browser restarts onto that server's WAD set, and
// the previous game then bleeds into the new one.
void ResetStartupStateForRestart();

// Reload the engine onto a new WAD set. `iwad` may be NULL/empty to keep the current IWAD; `pwads`
// replaces the current -file set (may be empty to run the bare IWAD). `startMap` (NULL/empty = none)
// makes the reload boot straight into that map instead of the title screen -- if the map isn't in the
// new WAD set, it lands at the title with a message rather than failing. Validates every wanted file
// first: if any is not loadable, returns InvalidWads and the running game is untouched. If the wanted
// set already matches what's loaded, returns AlreadyLoaded. Otherwise rewrites Args and throws
// CRestartException (so it does not return on that path).
//
// [rc4l] `connectAddress` (NULL/empty = none) makes the reload come back up connected to that server
// instead of at the title screen -- the server browser's "join" is this call. It rides the same
// mechanism as everything else here: RequestReload already STRIPS -connect from the rewritten Args
// (a reload must not silently rejoin whatever you were last connected to), so joining is that same
// argv gaining a -connect the engine's normal startup then acts on. Mutually exclusive with
// `startMap` in practice -- a server tells you which map you are on.
//
// Worth doing through here rather than `restart -connect ... -file ...`: this validates the whole
// wanted set BEFORE tearing anything down, so a truncated or garbage PWAD refuses the join and
// leaves you in the running game, instead of discovering it after the engine has already gone.
ReloadResult RequestReload(const char *iwad, const TArray<FString> &pwads, const char *startMap = NULL,
	const char *connectAddress = NULL);

}} // namespace zx::wadreload

#endif // ZX_WADRELOAD_H
