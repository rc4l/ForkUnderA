// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Turning a chosen catalogue entry into a server that can be started.
//
// The entry says what to load and which IWAD it wants; the host says what to call the server and how
// many people may be on it. This puts the two together and reports what is still missing, so the
// caller can decide between starting and downloading rather than finding out at launch.

#ifndef ZX_HOSTPLAN_COMPUTE_H
#define ZX_HOSTPLAN_COMPUTE_H

#include "features/addon-catalogue/computation/addonfile_compute.h"
#include "features/addon-catalogue/computation/iwadpick_compute.h"

#include <string>
#include <vector>

namespace zx
{

// The fields the host owns rather than the entry. An entry never names the server or picks the port:
// those belong to the person running it.
struct HostChoices
{
	std::string serverName;
	int maxPlayers;
	int port;
	bool advertise;

	HostChoices() : maxPlayers(8), port(0), advertise(false) {}
};

struct HostPlan
{
	bool ready;						// everything present; false means fetch `missing` first
	std::string iwad;				// what to start on, after substitution
	std::vector<std::string> pwads;	// bare filenames, in the entry's load order
	std::string execCfg;			// the entry's server.cfg, or "" when it has none

	// [rc4l] The chosen remixes' cfgs, exec'd AFTER the one above so they win, and in group order so a
	// later axis wins over an earlier one. Separate files rather than one merged one because they are
	// edited by different people for different reasons: the entry's says how the pack plays, each
	// remix's says the one thing it changes about any pack.
	//
	// A list because there is one per axis now. Remixes with no cfg contribute nothing and are simply
	// absent, so this is usually shorter than the number of axes.
	std::vector<std::string> execRemixCfgs;
	std::string map;				// the map to open on, or "" to let the cfg's rotation decide
	std::string serverName;
	int maxPlayers;
	int port;
	bool advertise;

	std::vector<std::string> missing;	// bare filenames not on disk, in load order

	// [rc4l] Whether the first of `missing` is the GAME rather than a mod, so the caller can ask the
	// downloader for it as one. The download path checks a hash against the shipped list before
	// keeping anything it was told is an iwad, and it can only do that if it is told.
	//
	// Only ever set for an iwad we can name as freely redistributable. A commercial one is still a
	// blocker, because the objection to fetching it was never that we lacked the plumbing.
	bool missingIwad;

	std::string blocker;				// why not ready at all; "" when only downloads are needed

	HostPlan() : ready(false), maxPlayers(8), port(0), advertise(false), missingIwad(false) {}
};

// `haveFiles` is the bare filenames already resolvable, which the caller gets from the by-hash store
// and the ordinary search path. Passing it in keeps this unit pure and keeps one notion of "present".
//
// A missing PWAD is not a blocker: it is a download, and the whole point of shipping hashes is that
// the downloader can go and get it.
//
// A missing IWAD is a blocker ONLY when we cannot name it as free. One we can is fetched like
// anything else -- there was never a technical reason not to, only a licensing one, and that reason
// does not apply to a game whose authors give it away. `freeIwad` answers that question; the caller
// passes the allowlist's verdict in rather than this unit knowing the list, which keeps it pure.
// `files` is what the CHOSEN WAY OF PLAYING loads, which PickVariant resolves: the entry's own files
// followed by the variant's. Passed in rather than read off the entry, because an entry no longer
// has one answer -- two variants of the same pack can load entirely different things, and reading
// addon.files here would plan for whichever of them happened to be listed at the top.
//
// `map` and `serverCfgPath` are likewise passed rather than read off the entry: which map to open on
// is the chosen variant's answer when it has one, and which cfg to exec depends on the variant too.
HostPlan BuildHostPlan(const AddonEntry &addon,
                       const std::vector<AddonFileRef> &files,
                       const IwadPick &iwad,
                       const std::string &serverCfgPath,
                       const std::vector<std::string> &remixCfgPaths,
                       const std::string &map,
                       const HostChoices &choices,
                       const std::vector<std::string> &haveFiles,
                       bool freeIwad = false);

} // namespace zx

#endif // ZX_HOSTPLAN_COMPUTE_H
