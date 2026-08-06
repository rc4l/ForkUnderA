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

#include "cl_main.h"		// CLIENT_GetConnectionState, CTS_ACTIVE
#include "network.h"		// NETWORK_GetState
#include "v_video.h"		// screen, DTA_*, SCREENWIDTH/HEIGHT -- the ready-to-join line
#include "v_font.h"			// SmallFont
#include "doomstat.h"		// gametic
#include "menu/menu.h"		// menuactive -- the notice hides while a menu is up

#include "features/server-browser/browser.h"
#include "features/server-browser/zx_joinserver.h"
#include "features/server-browser/computation/joinplan_compute.h"
#include "features/server-browser/computation/joinresume_compute.h"
#include "features/server-browser/computation/stableline_compute.h"
#include "features/wad-download/computation/iwadsubstitute_compute.h"
#include "features/wad-download/zx_filehash.h"
#include "features/wad-download/zx_wadsearch.h"
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

// Hex digests differ only in case between sources -- ours are lowercase, a server's are whatever it
// felt like. Compare them the way they are meant to be equal.
bool join_HexEquals(const char *a, const std::string &b)
{
	size_t i = 0;
	for (; ( a[i] != '\0' ) && ( i < b.size( )); ++i)
	{
		const char ca = (( a[i] >= 'A' ) && ( a[i] <= 'Z' )) ? char( a[i] - 'A' + 'a' ) : a[i];
		const char cb = (( b[i] >= 'A' ) && ( b[i] <= 'Z' )) ? char( b[i] - 'A' + 'a' ) : b[i];
		if (ca != cb)
			return false;
	}
	return ( a[i] == '\0' ) && ( i == b.size( ));
}

// Everything the join needs, in the server's own spelling, held apart from the browser's row index.
// It has to survive the download: a transfer takes minutes, and by the time it finishes the list may
// have been refreshed and re-sorted under the player, so resuming from "the selected server" would
// resume onto a different server.
struct JoinPlan
{
	FString iwadName;					// bare name, may be empty
	// [rc4l] MD5 of the BUILD the server runs (SQF2_FUA_IWAD_HASH), empty from a server that did not
	// send it. A name alone cannot distinguish nine releases of doom2.wad from each other.
	FString iwadHash;
	std::vector<std::string> wads;		// bare PWAD names, in the server's order
	// [rc4l] The server's own MD5 for each PWAD, keyed by the name it goes with rather than by index:
	// ComputeJoinWadList drops blanks and duplicates, so positions in `wads` no longer line up with
	// the server's original list. Empty for a server that sent no hashes.
	std::map<std::string, std::string> wadHashes;
	FString address;
	// [rc4l] Only ever used to name the server in a message when something goes wrong.
	FString serverName;
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

// [rc4l] The resume that follows a finished download, held while the browser is asking the player
// something they have to answer first.
//
// Without this a download completing one frame into a "cancel this download?" prompt would tear the
// engine down for the reload underneath the prompt -- the player is answering a question about a
// transfer that has already resolved itself, and the answer never lands. A restart appearing out of a
// dialog you were mid-way through is the kind of thing that reads as a crash.
bool g_resumeHeld = false;
bool g_resumePending = false;
bool g_resumePendingSuccess = false;

// [rc4l] A finished download whose join is waiting for the player to come back to it.
bool g_readyPending = false;
FString g_readyName;

void OnDownloadFinished(bool allSucceeded)
{
	// [rc4l] The decision itself is computation/joinresume_compute.h, where every combination of
	// "did it work / is the browser open / are they mid-answer" can be asserted. Getting it wrong
	// throws away whatever the player was doing, and each individual branch reads fine in review,
	// which is exactly the sort of thing that wants a truth table rather than a chain of ifs.
	const zx::ResumeAction action = zx::ComputeResumeAction( g_pending.valid, allSucceeded,
		zx::IsServerBrowserOpen(), g_resumeHeld );

	switch (action)
	{
	case zx::ResumeAction::Nothing:
		return;

	case zx::ResumeAction::Hold:
		g_resumePending = true;
		g_resumePendingSuccess = allSucceeded;
		return;

	case zx::ResumeAction::NotifyReady:
		// The join WAITS. g_pending is kept, and ConsumeJoinReady picks it up when the player next
		// opens the menu.
		g_readyPending = true;
		g_readyName = g_pending.serverName;
		Printf(TEXTCOLOR_GREEN "%s is ready to join.\n" TEXTCOLOR_NORMAL,
			g_readyName.IsNotEmpty() ? g_readyName.GetChars() : "That server");
		return;

	case zx::ResumeAction::ReportFailure:
		g_pending = JoinPlan();
		// The downloader has already said which file and why, on the console. This is the part the
		// player sees without having opened it.
		zx::ShowBrowserNotice("Couldn't get everything this server needs.\n\n"
			"See the console for what was missing.");
		return;

	case zx::ResumeAction::JoinNow:
		break;
	}

	// Taken by value and cleared BEFORE the retry: AttemptJoin does not return on the path that
	// works (RequestReload throws CRestartException), so anything after the call is unreachable
	// exactly when it matters.
	const JoinPlan plan = g_pending;
	g_pending = JoinPlan();

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

	// [rc4l] Teach the file search about Doomseeker's and GZDoom's folders before resolving anything:
	// a mod already downloaded through another tool should not be downloaded again.
	zx::RegisterKnownWadDirectories();

	if (plan.iwadName.IsNotEmpty())
	{
		// [rc4l] One name is not one file. doom2.wad shipped as 1.666, 1.7, 1.8, 1.9, a French build,
		// BFG Edition and the 2024 KEX re-release, and a player can easily hold two of them -- one
		// from Steam, one in their own WAD folder. Taking whichever the search order reaches first
		// means a coin flip, and losing it costs a join rejected by level authentication with a
		// message about nothing the player can act on.
		//
		// So when the server published its digest (SQF2_FUA_IWAD_HASH), look through every copy and
		// take the one that matches. Nothing is moved or renamed -- their collection stays theirs.
		if (plan.iwadHash.IsNotEmpty())
		{
			TArray<FString> candidates;
			zx::FindAllIwadsInEngineSearchPaths(plan.iwadName.GetChars(), candidates);

			for (unsigned i = 0; i < candidates.Size(); ++i)
			{
				char hex[33];
				if (!zx::Md5OfFile(candidates[i].GetChars(), hex, sizeof hex))
					continue;
				if (!join_HexEquals(hex, std::string(plan.iwadHash.GetChars())))
					continue;

				iwadPath = candidates[i];
				if (i > 0)
				{
					Printf(TEXTCOLOR_GREEN "Using the copy of %s that matches this server.\n"
						TEXTCOLOR_NORMAL, plan.iwadName.GetChars());
				}
				break;
			}

			if (iwadPath.IsEmpty() && candidates.Size() > 0)
			{
				// Say it plainly here rather than letting the connection fail later: level
				// authentication rejects a mismatched IWAD without ever naming the reason.
				Printf(TEXTCOLOR_GOLD "Your %s is a different build from the one this server runs.\n"
					TEXTCOLOR_NORMAL "The join may be rejected. Nothing is wrong with your copy -- "
					"that IWAD has shipped in several versions and they are not interchangeable.\n",
					plan.iwadName.GetChars());
			}
		}

		TArray<FString> iwadResolved;
		if (iwadPath.IsNotEmpty())
		{
			// Already settled by digest.
		}
		else if (D_AddFile(iwadResolved, plan.iwadName.GetChars()) && iwadResolved.Size() > 0)
		{
			iwadPath = iwadResolved[0];
		}
		else
		{
			// [rc4l] D_AddFile searches FileSearch.Directories -- the -file path. An IWAD lives
			// wherever the ENGINE looks for IWADs, which is a different list plus every Steam
			// library. Missing this told players who own Doom II on Steam that they did not, and
			// substituted Freedoom for a game sitting on their disk.
			iwadPath = zx::FindIwadInEngineSearchPaths(plan.iwadName.GetChars());
		}

		if (iwadPath.IsEmpty())
		{
			// [rc4l] Owning the server's IWAD always wins -- we only get here having failed to find
			// it. Freedoom is a from-scratch replacement for Doom's data, so a server on doom2.wad
			// is joinable without owning Doom II. Substituting is second-best and says so; refusing
			// the join outright would be worse for the case this mostly hits, which is a server
			// running a PWAD that replaces every map and uses the IWAD only for its resources.
			const std::string sub = cl_fua_iwad_substitute
				? zx::FreeIwadSubstituteFor(plan.iwadName.GetChars()) : std::string();

			// The substitute is an IWAD too, so it gets the same widened search -- a player whose
			// Freedoom came from Doomseeker or Steam should not be told to download it again.
			FString subPath;
			if (!sub.empty())
			{
				TArray<FString> subResolved;
				if (D_AddFile(subResolved, sub.c_str()) && subResolved.Size() > 0)
					subPath = subResolved[0];
				else
					subPath = zx::FindIwadInEngineSearchPaths(sub.c_str());
			}

			if (subPath.IsNotEmpty())
			{
				iwadPath = subPath;
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
		// Fold the case for the lookup: ComputeJoinWadList keeps the server's own spelling, and
		// the hash map was keyed the same way, but a server is free to be inconsistent.
		std::map<std::string, std::string>::const_iterator it = plan.wadHashes.find(plan.wads[i]);
		const std::string md5 = ( it != plan.wadHashes.end( )) ? it->second : std::string( );

		// [rc4l] Ask for the CONTENT before asking for the name. When the server told us a digest, a
		// copy we already hold with that digest is the right answer by definition, and going through
		// the name search instead is how a player's own test.wad -- earlier in
		// FileSearch.Directories than our download folder -- shadows the copy we just fetched for
		// this server. That path ends in "can't join" with the correct file sitting on disk.
		if (!md5.empty())
		{
			const FString exact = zx::waddownload::FindLocalCopy(plan.wads[i].c_str(), md5.c_str());
			if (exact.IsNotEmpty())
			{
				resolved.Push(exact);
				continue;					// already known to be the right bytes; nothing to check
			}
		}

		const unsigned beforeAdd = resolved.Size();
		if (D_AddFile(resolved, plan.wads[i].c_str()) == false)
		{
			missing.push_back(zx::waddownload::WantedFile(plan.wads[i], false, md5));
			continue;
		}

		// [rc4l] Having a file by that name is not the same as having THAT file, and the difference
		// is what makes iterating on a WAD work more than once. Edit test.wad, restart the server,
		// and the name has not changed -- so a check that stops at "found it" loads yesterday's
		// bytes and fails level authentication with a message about nothing that is wrong.
		//
		// Only checked when the server actually published a digest (SQF2_PWAD_HASHES); an older
		// server that sent none leaves this empty, which means "cannot compare", never "matches".
		if (md5.empty() || resolved.Size() <= beforeAdd)
			continue;

		const FString path = resolved[resolved.Size() - 1];
		char hex[33];
		if (zx::Md5OfFile(path.GetChars(), hex, sizeof hex) == false)
			continue;					// unreadable: let the loader be the one to complain

		if (join_HexEquals(hex, md5))
			continue;

		// Stale. Roll `resolved` back to where it was before this file so the loader never sees the
		// old copy -- truncating rather than popping once, because D_AddFile is free to have pushed
		// more than one entry for a single name.
		//
		// This cannot loop: OnDownloadFinished retries with downloading OFF, so a file that is still
		// wrong after a successful fetch ends as a plain "can't join" instead of a second attempt.
		Printf(TEXTCOLOR_GOLD "%s is not the copy this server is running; fetching it again.\n"
			TEXTCOLOR_NORMAL, plan.wads[i].c_str());
		while (resolved.Size() > beforeAdd)
			resolved.Pop();
		missing.push_back(zx::waddownload::WantedFile(plan.wads[i], false, md5));
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
		zx::ShowBrowserNotice(msg.GetChars());
		return false;
	}

	M_ClearMenus();

	// [rc4l] Marked BEFORE the reload, because the reload does not return: RequestReload throws
	// CRestartException on the path that works. Anything recorded after it would never run.
	zx::NoteJoinStarted(plan.serverName.GetChars());

	// RequestReload either connects in place (already on this WAD set), or throws CRestartException
	// and does not return. InvalidWads is the only way back here with nothing done.
	const zx::wadreload::ReloadResult r = zx::wadreload::RequestReload(
		iwadPath.IsNotEmpty() ? iwadPath.GetChars() : NULL, resolved, NULL, plan.address.GetChars());

	if (r == zx::wadreload::ReloadResult::InvalidWads)
	{
		zx::NoteJoinSucceeded();	// nothing is in flight; clear the mark so a later quit is not blamed
		zx::ShowBrowserNotice("Can't join: one of the server's files is not a loadable WAD.");
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
		zx::ShowBrowserNotice("No server selected.");
		return false;
	}

	// [rc4l] A transfer already running is no longer a refusal. It used to be -- "still downloading,
	// use fua_download_stop" -- which is what made picking a second server a dead end, and worse, the
	// FIRST download would then finish and drag the player onto the server they had moved away from.
	// waddownload::Start now abandons the old run and queues this one, so the guard that prevented it
	// has to go with it.

	const char *pszIwad = BROWSER_GetIWADName((ULONG)lServer);

	JoinPlan plan;
	plan.iwadName = pszIwad != NULL ? pszIwad : "";
	plan.iwadHash = BROWSER_GetIWADHash((ULONG)lServer);
	plan.serverName = BROWSER_GetHostName((ULONG)lServer);

	// [rc4l] Cheapest possible pre-flight: the browser queried this server seconds ago, so if it was
	// full then it is almost certainly full now. Checking here costs nothing and avoids the worst
	// outcome -- tearing the game down for a reload and then being refused, which used to leave the
	// player at a bare console. It cannot close the race entirely (someone can take the last slot
	// while we reload), which is what the failed-join landing is for.
	const LONG lPlayers = BROWSER_GetNumPlayers((ULONG)lServer);
	const LONG lMaxClients = BROWSER_GetMaxClients((ULONG)lServer);
	if (( lMaxClients > 0 ) && ( lPlayers >= lMaxClients ))
	{
		FString msg;
		msg.Format("%s is full (%d/%d).\n\nNothing has been changed -- try again when a slot opens."
			"\n\npress a key.", plan.serverName.GetChars(), static_cast<int>( lPlayers ),
			static_cast<int>( lMaxClients ));
		zx::ShowBrowserNotice(msg.GetChars());
		return false;
	}
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

	// [rc4l] The server's own endpoint, if it serves its files itself (SQF2_FUA_DIRECT_DOWNLOAD).
	//
	// FIRST by default, and that ordering is the point of the feature rather than an optimisation.
	// The case a mirror cannot cover is a WAD built this afternoon, which exists nowhere else; and
	// even when mirrors do have the file, the server's copy is by definition the one matching the
	// MD5 it advertises, so it is the copy that verifies on the first try instead of after two
	// mirrors served a different build under the same name.
	//
	// An operator who hosts their WADs somewhere with real bandwidth can flip it, which is what
	// sv_fua_download_prefermirrors is for -- they are the one paying for the traffic.
	const FString directUrl = BROWSER_GetDirectDownloadURL((ULONG)lServer);
	if (directUrl.IsNotEmpty())
	{
		if (BROWSER_PrefersMirrors((ULONG)lServer))
			plan.sites.push_back(directUrl.GetChars());
		else
			plan.sites.insert(plan.sites.begin(), directUrl.GetChars());
	}

	plan.valid = true;
	return AttemptJoin(plan, true);
}

//*****************************************************************************
//
// [rc4l] A join in flight, and where it should land if it fails.
//
// These survive the WAD reload deliberately. RequestReload throws CRestartException, which unwinds
// and re-runs the startup -- the PROCESS lives, so a static here keeps its value across it. That is
// the only way to know, after the restart, that the connect being attempted is one we started rather
// than something the player typed.
bool g_joinInFlight = false;
FString g_joiningName;
bool g_joinFailed = false;
FString g_joinFailReason;

void HoldJoinResume()
{
	g_resumeHeld = true;
}

void NoteJoinStarted( const char *serverName )
{
	g_joinInFlight = true;
	g_joiningName = ( serverName != NULL ) ? serverName : "";
	g_joinFailed = false;
	g_joinFailReason = "";
}

void NoteJoinSucceeded()
{
	g_joinInFlight = false;
}

void NoteJoinFailed( const char *reason )
{
	// Only a connect WE started. Quitting a server normally, or being kicked from one an hour later,
	// is not a failed join and must not drag the player back into the browser.
	if ( !g_joinInFlight )
		return;

	// [rc4l] A NULL reason is a routine teardown, not a refusal.
	//
	// CLIENT_QuitNetworkGame is the disconnect path for EVERYTHING -- including the tidy-up a
	// successful join does on its way in -- and it is called with no message on those. Treating that
	// as a failure is what produced "Couldn't join Filler14." on screen while the player was
	// spectating on Filler14, with no reason under it because there was never a reason to give.
	//
	// The engine already draws this distinction: it passes a string exactly when it has something to
	// say about why the connection ended.
	if (( reason == NULL ) || ( reason[0] == '\0' ))
		return;

	g_joinInFlight = false;
	g_joinFailed = true;
	g_joinFailReason = ( reason != NULL ) ? reason : "";

	// Recorded rather than acted on: this is called from the middle of the disconnect, and opening a
	// menu while the game is still tearing itself down is asking for it. JoinTick does the rest.
}

void JoinTick()
{
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		return;

	// Connected and playing -- whatever happens from here is not a failed join.
	if ( g_joinInFlight && ( CLIENT_GetConnectionState( ) == CTS_ACTIVE ))
		NoteJoinSucceeded( );

	if ( !g_joinFailed )
		return;
	g_joinFailed = false;

	// Back to the list, with the reason, instead of a bare console. The WAD set stays loaded, so
	// picking another server on the same files needs no second reload.
	FString message;
	if ( g_joiningName.IsNotEmpty( ))
		message.Format( TEXTCOLOR_GOLD "%s: %s" TEXTCOLOR_NORMAL, g_joiningName.GetChars( ),
			g_joinFailReason.GetChars( ));
	else
		message.Format( TEXTCOLOR_GOLD "%s" TEXTCOLOR_NORMAL, g_joinFailReason.GetChars( ));

	Printf( "%s\n", message.GetChars( ));

	M_StartControlPanel( false );
	M_SetMenu( "ZA_Browser", -1 );

	// On the browser's own panel, so the reason arrives with the list rather than over a title screen.
	FString notice;
	if ( g_joiningName.IsNotEmpty( ))
		notice.Format( "Couldn't join %s.\n\n%s", g_joiningName.GetChars( ), g_joinFailReason.GetChars( ));
	else
		notice = g_joinFailReason;
	ShowBrowserNotice( notice.GetChars( ));
}

void ReleaseJoinResume(bool proceed)
{
	g_resumeHeld = false;

	if (!proceed)
	{
		// [rc4l] The player said stop, so the JOIN is abandoned here -- unconditionally, not only in
		// the race below where the transfer had already finished.
		//
		// Leaving it pending meant the aborted transfer reached OnDownloadFinished looking exactly
		// like a failed one, and the player was told "couldn't get everything this server needs, see
		// the console for what was missing" -- a diagnosis, and a homework assignment, for something
		// they had just chosen on purpose and confirmed.
		g_pending = JoinPlan();

		if (g_resumePending)
		{
			// It finished while they were being asked. The file stays -- it is downloaded and
			// verified, and throwing it away would only mean fetching it again -- but the join it was
			// for does not happen, because that is what they answered.
			g_resumePending = false;
			Printf(TEXTCOLOR_GOLD "The download had already finished, so the file is kept -- but the "
				"join was cancelled as you asked.\n" TEXTCOLOR_NORMAL);
		}
		return;
	}

	if (!g_resumePending)
		return;

	const bool succeeded = g_resumePendingSuccess;
	g_resumePending = false;
	OnDownloadFinished(succeeded);
}

bool IsJoinResumeHeld()
{
	return g_resumeHeld;
}

bool ConsumeJoinReady()
{
	if ( !g_readyPending )
		return false;

	// Cleared, but g_pending is left alone: the player is being taken to the browser, and pressing
	// JOIN there starts the join from scratch with files that are now already on disk.
	g_readyPending = false;
	g_readyName = "";
	return true;
}

// [rc4l] The widest digit in SmallFont, measured once. Which one it is depends on the font, so it is
// found rather than assumed -- and found here rather than per frame, since it cannot change.
static int WidestDigit( )
{
	static char cached = 0;
	if ( cached != 0 )
		return cached;

	int best = -1;
	for ( char c = '0'; c <= '9'; ++c )
	{
		char one[2] = { c, 0 };
		const int w = SmallFont->StringWidth( one );
		if ( w > best )
		{
			best = w;
			cached = c;
		}
	}

	return cached;
}

void DrawJoinReadyNotice( bool afterMenus )
{
	// [rc4l] Drawn over EVERYTHING, menus included.
	//
	// It used to bail while a menu was up, on the reasoning that the browser's own footer carried the
	// transfer and anything else was the player doing something deliberate. That was wrong in the way
	// that matters: a player who wanders into the options menu mid-download loses the only readout
	// there is, and the browser then had to duplicate the same line in its footer to cover the one
	// case it did handle. One band, one place, always visible.
	//
	// WHICH PASS. D_Display offers this two moments: inside the level's 2D pass, and after the menu
	// has been drawn. Neither one alone works. Drawing after the menu is the only way to be on top of
	// it, but during ordinary play that point is past whatever actually commits 2D drawing -- the band
	// and the line are issued and never appear, which is precisely the bug this argument exists to
	// fix. So: menu up, draw after it; no menu, draw with the level.
	if ( afterMenus != ( menuactive != MENU_Off ))
		return;

	const FString progress = zx::waddownload::StatusLine( );
	const bool bReady = g_readyPending;

	if ( !bReady && progress.IsEmpty( ))
		return;

	// [rc4l] The same slot carries both states, because they are the same story: the thing you asked
	// for is on its way, and then it has arrived. Nothing about files or downloads in the ready
	// wording -- the transfer was our problem, and what the player cares about is the server.
	// [rc4l] The server's own name is deliberately NOT in this line. It is a name the server chose,
	// so it can be long, colourful or blank, and it pushed the one instruction in the sentence off
	// to the right where the eye reaches it last. The player knows which server they asked for --
	// they asked for it seconds ago -- and what they do not know is what to do about it.
	FString text;
	if ( bReady )
	{
		text = "Server is ready to join - Open the Menu";
	}
	else
	{
		text = progress;
	}

	// Ready pulses hard, because it wants to be noticed and then acted on. Progress barely moves --
	// it is there to be glanced at, not to compete with the game for attention.
	float alpha = 1.0f;
	if ( bReady )
	{
		const double phase = ( gametic % 46 ) / 46.0;
		alpha = 0.35f + 0.65f * static_cast<float>( fabs( 1.0 - 2.0 * phase ));
	}
	else
	{
		alpha = 0.72f;
	}

	const int virtW = 640;
	const int virtH = 400;
	const int y = 12;

	// [rc4l] The band is laid out from a MASKED copy of the line -- every digit replaced by whichever
	// digit is widest in this font -- rather than from the line itself.
	//
	// Padding the string to a fixed character count was not enough. SmallFont gives '1' a narrower
	// advance than '0', so "11%" and "80%" are different widths and the panel kept shuffling as the
	// numbers went by. The mask is the same string every frame whatever the numbers are, so its width
	// is a constant, and it is never narrower than the real line, so nothing overflows it.
	const FString stable = zx::MaskVarying( text.GetChars( ), WidestDigit( )).c_str( );
	const int stableW = SmallFont->StringWidth( stable );

	// BOTH are centred on the same axis, each on its own width. Placing the text at the box's left
	// edge instead left every pixel the mask over-measured sitting on one side, so the line looked
	// shoved against the end of its own panel.
	//
	// The text can still shift by a pixel or two as the digits change, since the mask is an upper
	// bound rather than an exact match -- but the character count is fixed, so that is glyph-width
	// variance and nothing more. The box, which is the thing that was moving, does not move at all.
	const int x = ( virtW / 2 ) - ( SmallFont->StringWidth( text ) / 2 );
	const int bandLeft = ( virtW / 2 ) - ( stableW / 2 );

	// A backing band, so the line stays readable over a bright wall or a lit sky.
	//
	// Placed through VirtualToRealCoordsInt, the SAME conversion DTA_VirtualWidth puts the text
	// through -- NOT Scale(), which is a plain stretch. DTA_Virtual* corrects for aspect ratio,
	// letterboxing the virtual space inside the window, so scaling by SCREENWIDTH/virtW agreed with
	// the text only on a display that happened to be 16:10 and put the band visibly off-centre under
	// it everywhere else. Same mistake, same fix, as the browser's own panel edges.
	int bandX = bandLeft - 6;
	int bandY = y - 3;
	int bandW = stableW + 12;
	int bandH = SmallFont->GetHeight( ) + 6;
	screen->VirtualToRealCoordsInt( bandX, bandY, bandW, bandH, virtW, virtH, false, true );

	screen->Dim( PalEntry( 0, 0, 0 ), 0.45f * alpha, bandX, bandY, bandW, bandH );

	// [rc4l] The text is drawn OPAQUE, and the pulse lives in the band behind it and in the colour.
	//
	// It used to carry DTA_Alpha, and the result was a line whose brightness depended on what had been
	// drawn just before it: full strength while the pointer was over a main-menu item, dimmer when it
	// was not. Translucent text picks up state the menu leaves behind, and this band is drawn after
	// everything precisely so it is not part of anyone else's pass.
	//
	// Nothing is lost. The backing band still fades with the pulse, and the ready state alternates
	// between gold and white, which reads harder across a room than an alpha wobble ever did.
	EColorRange colour = CR_GRAY;
	if ( bReady )
		colour = (( gametic % 46 ) < 23 ) ? CR_GOLD : CR_WHITE;

	screen->DrawText( SmallFont, colour, x, y, text,
		DTA_VirtualWidth, virtW, DTA_VirtualHeight, virtH, TAG_DONE );
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
