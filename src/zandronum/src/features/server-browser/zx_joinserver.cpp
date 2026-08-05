// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Joining a server from the browser: work out which files it wants, get the ones we are
// missing, and reload onto that set already connected.
//
// The WAD search is NOT reimplemented here. D_AddFile resolves a bare name through BaseFileSearch --
// the same lookup the command line uses, covering the progdir, the configured search paths and
// DOOMWADDIR -- and returns false when it cannot find one. So "do we have this?" and "where is it?"
// are the same call, and a player's WADs are found wherever the engine would already have found them.
// It is also why a downloaded file needs no special case: features/wad-download registers its folder
// as a search path, so the retry after a download finds it exactly like any other WAD.
//
// The reload goes through zx::wadreload rather than `restart -connect ... -file ...` (what the old
// browser did) because RequestReload validates the whole set BEFORE tearing the running game down. A
// truncated download refuses the join and leaves you where you were, instead of being discovered
// after the engine has already gone.

#include <map>
#include <string>
#include <vector>

#include "doomtype.h"
#include "c_console.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "v_text.h"
#include "d_main.h"		// D_AddFile
#include "menu/menu.h"	// M_StartMessage, M_ClearMenus

#include "features/server-browser/browser.h"
#include "features/server-browser/computation/joinplan_compute.h"
#include "features/wad-download/computation/iwadsubstitute_compute.h"
#include "features/wad-download/zx_waddownload.h"
#include "features/wadreload/zx_wadreload.h"

// [rc4l] Load Freedoom when the server's IWAD is a game you do not own. Only ever a fallback -- the
// server's real IWAD wins whenever it can be found -- and only for the games Freedoom actually
// replaces; see features/wad-download/computation/iwadsubstitute_compute.h.
//
// A CVAR because the player is the one who can tell which case they are in. On a server running a
// PWAD that replaces every map (most of the browser) substitution just works; on one running stock
// Doom II levels the geometry differs and Zandronum's level authentication rejects the client, and
// then this is the switch that gets the plain "you are missing doom2.wad" back.
CVAR( Bool, cl_fua_iwad_substitute, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG )

namespace
{

// Everything the join needs, in the server's own spelling, held apart from the browser's row index.
// It has to survive the download: a transfer takes minutes, and by the time it finishes the list may
// have been refreshed and re-sorted under the player, so resuming from "the selected server" would
// resume onto a different server.
struct JoinPlan
{
	FString iwadName;					// bare name, may be empty
	std::vector<std::string> wads;		// bare PWAD names, in the server's order
	// [rc4l] The server's own MD5 for each PWAD, keyed by the name it goes with rather than by index:
	// ComputeJoinWadList drops blanks and duplicates, so positions in `wads` no longer line up with
	// the server's original list. Empty for a server that sent no hashes.
	std::map<std::string, std::string> wadHashes;
	FString address;
	std::vector<std::string> sites;		// the server's own advertised download site, if any
	bool valid;

	JoinPlan() : valid(false) {}
};

JoinPlan g_pending;

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

bool AttemptJoin(const JoinPlan &plan, bool mayDownload);

void OnDownloadFinished(bool allSucceeded)
{
	if (!g_pending.valid)
		return;

	// Taken by value and cleared BEFORE the retry: AttemptJoin does not return on the path that
	// works (RequestReload throws CRestartException), so anything after the call is unreachable
	// exactly when it matters.
	const JoinPlan plan = g_pending;
	g_pending = JoinPlan();

	if (!allSucceeded)
	{
		// The downloader has already said which file and why, on the console. This is the part the
		// player sees without having opened it.
		M_StartMessage("Couldn't get everything this server needs.\n\n"
			"See the console for what was missing.\n\npress a key.", 1);
		return;
	}

	// Second pass, with downloading off: if something is STILL missing after a run that reported
	// success, retrying the download would loop forever on it.
	AttemptJoin(plan, false);
}

// Returns false and reports to the player if the join could not be started; never tears down on that
// path. A started download also returns false -- nothing has been joined yet, and OnDownloadFinished
// takes it from there.
bool AttemptJoin(const JoinPlan &plan, bool mayDownload)
{
	// [rc4l] The IWAD needs resolving too, and for the same reason the PWADs do: the server sends a
	// bare name ("doom2.wad") and the file is wherever the player keeps their WADs. Passing the name
	// through unresolved made RequestReload's loadability check test it against the working directory
	// and refuse every join whose IWAD was not sitting next to the exe -- which is nearly all of them,
	// since the whole point of the search path is that WADs live elsewhere.
	FString iwadPath;
	std::vector<zx::waddownload::WantedFile> missing;

	if (plan.iwadName.IsNotEmpty())
	{
		TArray<FString> iwadResolved;
		if (D_AddFile(iwadResolved, plan.iwadName.GetChars()) && iwadResolved.Size() > 0)
		{
			iwadPath = iwadResolved[0];
		}
		else
		{
			// [rc4l] Owning the server's IWAD always wins -- we only get here having failed to find
			// it. Freedoom is a from-scratch replacement for Doom's data, so a server on doom2.wad
			// is joinable without owning Doom II. Substituting is second-best and says so; refusing
			// the join outright would be worse for the case this mostly hits, which is a server
			// running a PWAD that replaces every map and uses the IWAD only for its resources.
			const std::string sub = cl_fua_iwad_substitute
				? zx::FreeIwadSubstituteFor(plan.iwadName.GetChars()) : std::string();

			TArray<FString> subResolved;
			if (!sub.empty() && D_AddFile(subResolved, sub.c_str()) && subResolved.Size() > 0)
			{
				iwadPath = subResolved[0];
				Printf(TEXTCOLOR_GOLD "This server wants %s, which you don't have. "
					"Loading %s instead.\n" TEXTCOLOR_NORMAL
					"If the server is on its own maps this works; on stock maps it will not, and "
					"cl_fua_iwad_substitute 0 turns it off.\n",
					plan.iwadName.GetChars(), sub.c_str());
			}
			else
			{
				// Ask for the substitute rather than the game itself: the substitute is on the
				// download allowlist, so this is a join that can actually complete. With no
				// substitute we ask for the original, and the gate refuses it with the reason.
				missing.push_back(zx::waddownload::WantedFile(
					sub.empty() ? plan.iwadName.GetChars() : sub, true));
			}
		}
	}

	// Resolve every name to a real file. D_AddFile pushes the RESOLVED path, so `resolved` ends up
	// being what we hand the loader -- no second lookup, and no chance of resolving to a different
	// file than the one we checked.
	TArray<FString> resolved;
	for (size_t i = 0; i < plan.wads.size(); ++i)
	{
		if (D_AddFile(resolved, plan.wads[i].c_str()) == false)
		{
			// Fold the case for the lookup: ComputeJoinWadList keeps the server's own spelling, and
			// the hash map was keyed the same way, but a server is free to be inconsistent.
			std::map<std::string, std::string>::const_iterator it = plan.wadHashes.find(plan.wads[i]);
			const std::string md5 = ( it != plan.wadHashes.end( )) ? it->second : std::string( );
			missing.push_back(zx::waddownload::WantedFile(plan.wads[i], false, md5));
		}
	}

	if (missing.size() > 0)
	{
		if (mayDownload && zx::waddownload::IsAvailable())
		{
			// The browser stays open rather than putting up a modal: the transfer runs for minutes,
			// and a dialog you cannot dismiss without cancelling is a worse way to spend them than a
			// progress line under a list you can still read. The join resumes on its own.
			if (zx::waddownload::Start(plan.sites, missing, OnDownloadFinished))
			{
				g_pending = plan;
				g_pending.valid = true;
				return false;
			}
			// Start() refused (downloads off, no sites, or every file blocked by the IWAD gate) and
			// has said why. Fall through to the plain "you are missing these" message.
		}

		// Name every missing file, not just the first: a player chasing them one restart at a time is
		// the worst version of this.
		FString msg = "Can't join. Missing:\n\n";
		for (size_t i = 0; i < missing.size(); ++i)
		{
			msg += missing[i].name.c_str();
			msg += "\n";
		}
		msg += "\npress a key.";
		M_StartMessage(msg.GetChars(), 1);
		return false;
	}

	M_ClearMenus();
	// RequestReload either connects in place (already on this WAD set), or throws CRestartException
	// and does not return. InvalidWads is the only way back here with nothing done.
	const zx::wadreload::ReloadResult r = zx::wadreload::RequestReload(
		iwadPath.IsNotEmpty() ? iwadPath.GetChars() : NULL, resolved, NULL, plan.address.GetChars());

	if (r == zx::wadreload::ReloadResult::InvalidWads)
	{
		M_StartMessage("Can't join: one of the server's files is not a loadable WAD.\n\npress a key.", 1);
		return false;
	}
	return true;
}

} // namespace

namespace zx
{

bool JoinSelectedServer()
{
	const LONG lServer = BROWSER_GetSelectedServer();
	if (lServer < 0)
	{
		M_StartMessage("No server selected.\n\npress a key.", 1);
		return false;
	}

	if (zx::waddownload::IsRunning())
	{
		M_StartMessage("Still downloading this server's files.\n\n"
			"Use fua_download_stop to give up on it.\n\npress a key.", 1);
		return false;
	}

	const char *pszIwad = BROWSER_GetIWADName((ULONG)lServer);

	JoinPlan plan;
	plan.iwadName = pszIwad != NULL ? pszIwad : "";
	plan.wads = ComputeJoinWadList(plan.iwadName.GetChars(), ServerPwadNames(lServer));

	// [rc4l] Pair each PWAD with the MD5 the server advertised (SQF2_PWAD_HASHES), so a downloaded
	// mod can be checked against what the server actually runs. Servers that send no hashes leave
	// this empty, which the downloader treats as "cannot check" rather than "fine".
	{
		const LONG lCount = BROWSER_GetNumPWADs((ULONG)lServer);
		for (LONG i = 0; i < lCount; ++i)
		{
			const char *pszName = BROWSER_GetPWADName((ULONG)lServer, (ULONG)i);
			const char *pszHash = BROWSER_GetPWADHash((ULONG)lServer, (ULONG)i);
			if (( pszName != NULL ) && ( pszHash != NULL ) && ( pszHash[0] != '\0' ))
				plan.wadHashes[pszName] = pszHash;
		}
	}
	plan.address = BROWSER_GetAddress((ULONG)lServer).ToString();

	// [rc4l] Where this server says its files live. Zandronum has advertised this since forever --
	// sv_website goes out over the launcher protocol as SQF_URL, and browser.h describes the field it
	// lands in as "Website URL of the wad the server is using". It is already exactly what Odamex
	// added sv_downloadsites for, so a ZandroX client can download from an ordinary Zandronum server
	// that has never heard of us. The player's own cl_fua_downloadsites is appended after it.
	const char *pszWadUrl = BROWSER_GetWadURL((ULONG)lServer);
	if (pszWadUrl != NULL && pszWadUrl[0] != '\0')
		plan.sites.push_back(pszWadUrl);

	plan.valid = true;
	return AttemptJoin(plan, true);
}

} // namespace zx

// [rc4l] fua_ per the naming rule: ours, no upstream equivalent. The old browser's
// menu_join_selected_server used `restart -connect`; this one goes through the validated reload.
CCMD(fua_join_selected_server)
{
	zx::JoinSelectedServer();
}

// [rc4l] Old spelling, kept alive on purpose. The pre-MVP browser owned this CCMD, and removing a
// command is a breaking change for anyone who bound it -- so it survives its menu, as a second name
// for the one validated implementation above rather than the `restart -connect` duplicate it used to
// be. Nothing in the tree calls it; only a user's config would.
CCMD ( menu_join_selected_server )
{
	zx::JoinSelectedServer();
}
