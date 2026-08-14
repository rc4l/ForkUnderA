// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <stdio.h>

#include "features/addon-catalogue/computation/teamspick_compute.h"

namespace zx
{

namespace
{

// [rc4l] No 1. See the header: one team is a free-for-all wearing a colour, and offering it would be
// offering a mistake. The ceiling is 4 because TEAMINFO defines four and sv_maxteams clamps itself
// to teams.Size() anyway; asking for more would be silently ignored rather than refused.
const int kStops[4] = { 0, 2, 3, 4 };

} // namespace

int TeamsStopCount()
{
	return 4;
}

int TeamsCountAtStop(int stop)
{
	if (stop <= 0)
		return kStops[0];
	if (stop >= TeamsStopCount() - 1)
		return kStops[TeamsStopCount() - 1];
	return kStops[stop];
}

int TeamsStopForCount(int count)
{
	// Anything below two is the free-for-all, which is where a stray 1 belongs: it is nearer to
	// nobody having teams than to a real side.
	if (count < 2)
		return 0;

	for (int i = 1; i < TeamsStopCount(); ++i)
	{
		if (kStops[i] >= count)
			return i;
	}

	return TeamsStopCount() - 1;
}

TeamsControl TeamsFor(HostGameMode mode, bool offered, int wanted)
{
	TeamsControl out;

	if (!offered)
	{
		out.reason = "This way of playing sets its own teams";
		return out;
	}

	switch (mode)
	{
	case HostGameMode::Deathmatch:
		out.soloCvar = "deathmatch";
		out.teamCvar = "teamplay";
		break;

	case HostGameMode::LastManStanding:
		out.soloCvar = "lastmanstanding";
		out.teamCvar = "teamlms";
		break;

	case HostGameMode::Possession:
		out.soloCvar = "possession";
		out.teamCvar = "teampossession";
		break;

	case HostGameMode::CaptureTheFlag:
	case HostGameMode::Skulltag:
	case HostGameMode::Teamgame:
		// [rc4l] Said separately from the modes below, because these three DO have sides and being
		// told they have no free-for-all reads as a bug in the entry.
		//
		// All honour sv_maxteams where their maps do not: the first two carry one flag per side, and
		// a Teamgame map carries one set of team starts per side, so a third team would spawn with
		// nothing to take, nothing to lose, and in All Out War's case nowhere to stand.
		out.reason = "The map decides the sides here, and it carries two";
		return out;

	default:
		// Named rather than lumped in with "no", because an entry that asked for the control and did
		// not get one is a mistake in the entry and the reader should be able to see which.
		out.reason = (mode == HostGameMode::Unknown)
			? "This experience does not say how it plays"
			: "This way of playing has no free-for-all to switch out of";
		return out;
	}

	out.shape = TeamsShape::Optional;
	out.applies = true;
	out.adjustable = true;

	// Nothing chosen is a free-for-all, which is how every one of these packs is played by default and
	// what the two pills this replaces had selected.
	out.stop = (wanted < 0) ? 0 : TeamsStopForCount(wanted);
	out.count = TeamsCountAtStop(out.stop);

	return out;
}

std::vector<std::pair<std::string, std::string> > TeamsCvars(const TeamsControl &control)
{
	std::vector<std::pair<std::string, std::string> > out;

	if (!control.applies)
		return out;

	if (control.count < 2)
	{
		out.push_back(std::make_pair(control.soloCvar, std::string("true")));
		return out;
	}

	char number[16];
	// Two to four, so this cannot truncate.
	snprintf(number, sizeof(number), "%d", control.count);

	// [rc4l] The gamemode first, then the count. sv_maxteams is CVAR_LATCH and takes effect at the
	// next map, which is the map the server has not loaded yet -- the host's arguments set it before
	// +map, so the first map already has it.
	out.push_back(std::make_pair(control.teamCvar, std::string("true")));
	out.push_back(std::make_pair(std::string("sv_maxteams"), std::string(number)));

	return out;
}

} // namespace zx
