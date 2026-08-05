// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Fetching the files a server wants that the player does not have, over HTTP, from mirrors --
// so "you are missing brutal.wad" stops being the end of a join attempt.
//
// The design is Odamex's, and the important part of it is what the GAME SERVER does: nothing. It
// serves no file bytes at all. Modern Odamex's SV_WantWad reads the request, prints "Downloading is
// disabled" and drops the client (server/src/sv_main.cpp); what replaced it is a server-advertised
// list of ordinary HTTP mirrors (sv_downloadsites) plus a client-side default list
// (cl_downloadsites), and the client fetches from those with libcurl.
//
// That is the answer to "how do you stop downloads lagging the server", and it is a better answer
// than any rate limiter: there is no in-band transfer to rate-limit. A 40 MB WAD never touches the
// game socket, never competes with ticcmds, and ten players joining at once costs the server the
// same as none of them joining. A bandwidth cap on an in-band transfer would still have had every
// download sharing the one socket, the one thread and the one player's connection budget -- the
// server would merely stutter more slowly.
//
// We do not need a protocol extension to carry the mirror list, because Zandronum already has the
// field: sv_website is advertised over the launcher protocol as SQF_URL and the browser stores it as
// SERVER_t::WadURL, described in browser.h as "Website URL of the wad the server is using". That is
// exactly sv_downloadsites, already deployed on every Zandronum server in the world. So the mirror
// list is the server's own WadURL first, then cl_fua_downloadsites -- and this works against servers
// that have never heard of us.
//
// What is ours rather than copied: the legality gate. Odamex keeps a denylist of commercial files;
// we assume every IWAD is commercial and keep an allowlist of the free ones instead. See
// computation/iwadallow_compute.h for why that direction is the one that holds.
//
// Division of labour with features/wadreload: this feature decides whether a file may be downloaded
// and puts it on disk; wadreload decides whether a file is loadable and refuses to restart onto a bad
// set. A truncated download is caught there, not here, so there is exactly one place that answers
// "can the engine load this".

#ifndef ZX_WADDOWNLOAD_H
#define ZX_WADDOWNLOAD_H

#include <string>
#include <vector>

#include "zstring.h"

namespace zx { namespace waddownload {

// One file a server asked for. `isIwad` is true only for the file the server declared as its IWAD --
// it selects the strict allowlist gate, so getting it wrong in the permissive direction would let a
// game through.
struct WantedFile
{
	std::string name;
	bool isIwad;

	WantedFile() : isIwad(false) {}
	WantedFile(const std::string &n, bool iwad) : name(n), isIwad(iwad) {}
};

// Called on the MAIN thread from Tick() when a run finishes, exactly once per Start().
typedef void (*CompleteProc)(bool allSucceeded);

// Whether downloading is possible at all right now: the cvar is on and nothing else is in flight.
bool IsAvailable();

bool IsRunning();

// Begin fetching `files`, in order, from `extraSites` followed by cl_fua_downloadsites. Returns false
// (having done nothing) if downloading is off, a run is already in flight, the file list is empty, or
// every wanted file is refused by the legality gate -- refusals are printed either way. `onDone` may
// be NULL.
bool Start(const std::vector<std::string> &extraSites, const std::vector<WantedFile> &files,
	CompleteProc onDone);

// Stop the run. The partial file is discarded; the completion callback still fires (with false).
void Cancel();

// A one-line progress summary for the console or a menu, or "" when idle -- e.g.
// "brutal.wad  43%  (3.2 of 7.4 MB)".
FString StatusLine();

// Call once per frame from the main loop (D_DoomLoop). Prints whatever the worker queued and fires
// the completion callback. A no-op when idle.
void Tick();

// Where finished files are written. Created on demand, and registered in the config's
// FileSearch.Directories so BaseFileSearch finds what we downloaded -- this run and every run after.
FString DownloadDir();

}} // namespace zx::waddownload

#endif // ZX_WADDOWNLOAD_H
