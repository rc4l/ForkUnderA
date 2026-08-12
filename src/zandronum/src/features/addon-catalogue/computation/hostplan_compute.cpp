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
                       const std::vector<AddonFileRef> &files,
                       const IwadPick &iwad,
                       const std::string &serverCfgPath,
                       const std::vector<std::string> &remixCfgPaths,
                       const std::string &map,
                       const HostChoices &choices,
                       const std::vector<std::string> &haveFiles,
                       bool freeIwad)
{
	HostPlan plan;

	plan.serverName = choices.serverName;
	plan.maxPlayers = choices.maxPlayers;
	plan.port = choices.port;
	plan.advertise = choices.advertise;
	plan.execCfg = serverCfgPath;
	plan.map = map;

	// Empties dropped here rather than at every call site: a remix with no cfg is the ordinary case,
	// not something the caller should have to filter before it can ask for a plan.
	for (size_t i = 0; i < remixCfgPaths.size(); ++i)
	{
		if (!remixCfgPaths[i].empty())
			plan.execRemixCfgs.push_back(remixCfgPaths[i]);
	}

	if (!addon.valid)
	{
		plan.blocker = "this entry could not be read";
		return plan;
	}

	// [rc4l] Nothing on this machine to run on. Whether that is fatal turns on ONE question: can the
	// game be named as freely redistributable?
	//
	// If it can, it is a download like any other. The objection to fetching an iwad was always a
	// licensing one rather than a technical one, and it simply does not apply to a game whose authors
	// give it away. If it cannot, it stays a refusal, and the wording says why rather than implying
	// the file is merely absent.
	if (iwad.choice == IwadChoice::None)
	{
		if (addon.iwad.empty())
		{
			plan.blocker = "no IWAD to run on";
			return plan;
		}

		if (!freeIwad)
		{
			plan.blocker = "you need " + addon.iwad +
				", and there is no free stand-in for it on this machine";
			return plan;
		}

		// FIRST in the list, because it is the thing everything else sits on: a caller showing
		// progress reads better counting up from the game to the mods than the other way about.
		plan.iwad = addon.iwad;
		plan.missingIwad = true;
		plan.missing.push_back(addon.iwad);
	}
	else
	{
		plan.iwad = iwad.iwad;
	}

	// Load order is as resolved, unchanged. It is the only thing that says the announcer goes after
	// the maps, and re-deriving it here would be a second opinion nobody asked for.
	for (size_t i = 0; i < files.size(); ++i)
	{
		const std::string &name = files[i].name;

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
