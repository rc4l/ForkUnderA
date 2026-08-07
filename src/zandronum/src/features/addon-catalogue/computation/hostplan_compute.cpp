// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/addon-catalogue/computation/hostplan_compute.h"

#include <cctype>

namespace zx
{

namespace
{

bool SameFile(const std::string &a, const std::string &b)
{
	if (a.size() != b.size())
		return false;

	for (size_t i = 0; i < a.size(); ++i)
	{
		if (std::tolower(static_cast<unsigned char>(a[i])) !=
			std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	}
	return true;
}

bool Have(const std::vector<std::string> &haveFiles, const std::string &name)
{
	for (size_t i = 0; i < haveFiles.size(); ++i)
	{
		if (SameFile(haveFiles[i], name))
			return true;
	}
	return false;
}

} // namespace

HostPlan BuildHostPlan(const AddonEntry &addon,
                       const IwadPick &iwad,
                       const std::string &serverCfgPath,
                       const HostChoices &choices,
                       const std::vector<std::string> &haveFiles)
{
	HostPlan plan;

	plan.serverName = choices.serverName;
	plan.maxPlayers = choices.maxPlayers;
	plan.port = choices.port;
	plan.advertise = choices.advertise;
	plan.execCfg = serverCfgPath;

	if (!addon.valid)
	{
		plan.blocker = "this entry could not be read";
		return plan;
	}

	// An IWAD is the one thing that cannot be worked around. The substitute table has already been
	// consulted by PickIwad, so None here means there is genuinely nothing to run on, and offering a
	// download would be offering a commercial game we are not allowed to fetch.
	if (iwad.choice == IwadChoice::None)
	{
		plan.blocker = addon.iwad.empty()
			? std::string("no IWAD to run on")
			: ("you need " + addon.iwad + ", and there is no free stand-in for it on this machine");
		return plan;
	}

	plan.iwad = iwad.iwad;

	// Load order is the entry's, unchanged. It is the only thing that says the announcer goes after
	// the maps, and re-deriving it here would be a second opinion nobody asked for.
	for (size_t i = 0; i < addon.files.size(); ++i)
	{
		const std::string &name = addon.files[i].name;

		plan.pwads.push_back(name);

		if (!Have(haveFiles, name))
			plan.missing.push_back(name);
	}

	// Missing PWADs are a download rather than a refusal: shipping the hashes is what makes that
	// possible, so treating an absent file as fatal would waste the whole design.
	plan.ready = plan.missing.empty();
	return plan;
}

} // namespace zx
