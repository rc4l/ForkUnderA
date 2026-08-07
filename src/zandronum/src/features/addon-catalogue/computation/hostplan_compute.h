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
	std::string serverName;
	int maxPlayers;
	int port;
	bool advertise;

	std::vector<std::string> missing;	// bare filenames not on disk, in load order
	std::string blocker;				// why not ready at all; "" when only downloads are needed

	HostPlan() : ready(false), maxPlayers(8), port(0), advertise(false) {}
};

// `haveFiles` is the bare filenames already resolvable, which the caller gets from the by-hash store
// and the ordinary search path. Passing it in keeps this unit pure and keeps one notion of "present".
//
// A missing PWAD is not a blocker: it is a download, and the whole point of shipping hashes is that
// the downloader can go and get it. A missing IWAD IS a blocker, because there is nothing to
// substitute and nothing to fetch.
HostPlan BuildHostPlan(const AddonEntry &addon,
                       const IwadPick &iwad,
                       const std::string &serverCfgPath,
                       const HostChoices &choices,
                       const std::vector<std::string> &haveFiles);

} // namespace zx

#endif // ZX_HOSTPLAN_COMPUTE_H
