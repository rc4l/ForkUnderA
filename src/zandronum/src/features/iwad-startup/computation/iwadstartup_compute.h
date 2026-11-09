// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which IWAD a launch starts with, and whether the IWAD dialog asks, from the IWADs found.

#ifndef ZX_IWADSTARTUP_COMPUTE_H
#define ZX_IWADSTARTUP_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

// [rc4l] What the IWAD dialog returns after adding a WAD folder, so the search runs again and the dialog shows its IWADs, without a restart.
const int kIwadPickRescan = -2;

// [rc4l] One IWAD the search found: its file name without extension, and its IWADINFO Autoname.
struct IwadStartupCandidate
{
	std::string base;
	std::string autoname;
};

// [rc4l] The IWAD to preselect (or start, when nothing asks) and whether the dialog should ask.
struct IwadStartupChoice
{
	int pick;
	bool ask;
};

// [rc4l] Whether an Autoname is one of Freedoom's, which ships with the engine as the fallback game.
bool ComputeIsFreedoomIwad(const std::string &autoname);

// [rc4l] Freedoom alone starts without asking; anything else asks when queryiwad does or when an IWAD of the player's own is new since `seen`, preselecting that new IWAD over a saved Freedoom pick.
IwadStartupChoice ComputeIwadStartup(const std::vector<IwadStartupCandidate> &found,
	const std::string &savedDefault, const std::string &seen, bool queryiwad);

// [rc4l] The found IWADs' names as fua_iwads_seen keeps them, lower case and ';'-separated.
std::string ComputeIwadSeenList(const std::vector<IwadStartupCandidate> &found);

// [rc4l] The first of a folder's entries spelling `name` in any case (DOOM2.WAD for doom2.wad), or "" when none does.
std::string ComputeFindNameIgnoringCase(const std::vector<std::string> &entries, const std::string &name);

} // namespace zx

#endif // ZX_IWADSTARTUP_COMPUTE_H
