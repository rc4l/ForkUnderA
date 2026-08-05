// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Joining a server from the browser: work out which files it wants, check we have them, and
// reload onto that set already connected.
//
// The WAD search is NOT reimplemented here. D_AddFile resolves a bare name through BaseFileSearch --
// the same lookup the command line uses, covering the progdir, the configured search paths and
// DOOMWADDIR -- and returns false when it cannot find one. So "do we have this?" and "where is it?"
// are the same call, and a player's WADs are found wherever the engine would already have found them.
//
// The reload goes through zx::wadreload rather than `restart -connect ... -file ...` (what the old
// browser did) because RequestReload validates the whole set BEFORE tearing the running game down. A
// truncated download refuses the join and leaves you where you were, instead of being discovered
// after the engine has already gone.

#include "doomtype.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "d_main.h"		// D_AddFile
#include "menu/menu.h"	// M_StartMessage, M_ClearMenus

#include "features/server-browser/browser.h"
#include "features/server-browser/computation/joinplan_compute.h"
#include "features/wadreload/zx_wadreload.h"

namespace
{

std::vector<std::string> ServerPwadNames(LONG lServer)
{
	std::vector<std::string> out;
	const LONG lCount = BROWSER_GetNumPWADs((ULONG)lServer);
	for (LONG i = 0; i < lCount; ++i)
	{
		const char *name = BROWSER_GetPWADName((ULONG)lServer, (ULONG)i);
		out.push_back(name != NULL ? name : "");
	}
	return out;
}

} // namespace

namespace zx
{

// Returns false and reports to the player if anything is missing; never tears down on that path.
bool JoinSelectedServer()
{
	const LONG lServer = BROWSER_GetSelectedServer();
	if (lServer < 0)
	{
		M_StartMessage("No server selected.\n\npress a key.", 1);
		return false;
	}

	const char *pszIwad = BROWSER_GetIWADName((ULONG)lServer);
	const std::vector<std::string> wanted =
		ComputeJoinWadList(pszIwad != NULL ? pszIwad : "", ServerPwadNames(lServer));

	// [rc4l] The IWAD needs resolving too, and for the same reason the PWADs do: the server sends a
	// bare name ("doom2.wad") and the file is wherever the player keeps their WADs. Passing the name
	// through unresolved made RequestReload's loadability check test it against the working directory
	// and refuse every join whose IWAD was not sitting next to the exe -- which is nearly all of them,
	// since the whole point of the search path is that WADs live elsewhere.
	FString iwadPath;
	if (pszIwad != NULL && pszIwad[0] != '\0')
	{
		TArray<FString> iwadResolved;
		if (D_AddFile(iwadResolved, pszIwad) && iwadResolved.Size() > 0)
		{
			iwadPath = iwadResolved[0];
		}
		else
		{
			FString msg;
			msg.Format("Can't join. Missing the IWAD:\n\n%s\n\npress a key.", pszIwad);
			M_StartMessage(msg.GetChars(), 1);
			return false;
		}
	}

	// Resolve every name to a real file first. D_AddFile pushes the RESOLVED path, so `resolved` ends
	// up being what we hand the loader -- no second lookup, and no chance of resolving to a different
	// file than the one we checked.
	TArray<FString> resolved;
	TArray<FString> missing;
	for (size_t i = 0; i < wanted.size(); ++i)
	{
		if (D_AddFile(resolved, wanted[i].c_str()) == false)
			missing.Push(FString(wanted[i].c_str()));
	}

	if (missing.Size() > 0)
	{
		// Name every missing file, not just the first: a player chasing them one restart at a time is
		// the worst version of this.
		FString msg = "Can't join. Missing:\n\n";
		for (unsigned i = 0; i < missing.Size(); ++i)
		{
			msg += missing[i];
			msg += "\n";
		}
		msg += "\npress a key.";
		M_StartMessage(msg.GetChars(), 1);
		return false;
	}

	const FString address = BROWSER_GetAddress((ULONG)lServer).ToString();

	M_ClearMenus();
	// RequestReload either connects in place (already on this WAD set), or throws CRestartException
	// and does not return. InvalidWads is the only way back here with nothing done.
	const zx::wadreload::ReloadResult r = zx::wadreload::RequestReload(
		iwadPath.IsNotEmpty() ? iwadPath.GetChars() : NULL, resolved, NULL, address.GetChars());

	if (r == zx::wadreload::ReloadResult::InvalidWads)
	{
		M_StartMessage("Can't join: one of the server's files is not a loadable WAD.\n\npress a key.", 1);
		return false;
	}
	return true;
}

} // namespace zx

// [rc4l] fua_ per the naming rule: ours, no upstream equivalent. The old browser's
// menu_join_selected_server used `restart -connect`; this one goes through the validated reload.
CCMD(fua_join_selected_server)
{
	zx::JoinSelectedServer();
}
