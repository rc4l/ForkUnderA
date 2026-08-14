// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/servervar_compute.h"

namespace zx
{

const std::vector<ServerVar> &ServerVarTable()
{
	static std::vector<ServerVar> table;

	if (!table.empty())
		return table;

	// [rc4l] Seeded from what our own catalogue cfgs set, which is a real sample of what hosting
	// needs rather than a guess. Grouped the way somebody hosting thinks about them.

	// Respawning and the clock.
	table.push_back(ServerVar("sv_forcerespawntime", "Forced respawn delay", VarKind::Number, "0"));

	// Friendly fire. NOT sv_teamdamage: that cvar does not exist, and one of our own cfgs set it
	// for years while doing nothing at all. The real one is teamdamage, and it is a scalar rather
	// than a switch -- 0 is none, 1 is full.
	table.push_back(ServerVar("teamdamage", "Team damage", VarKind::Fraction, "0"));

	// Aim assistance, which is an Int with named values rather than a toggle.
	table.push_back(ServerVar("sv_smartaim", "Smart autoaim", VarKind::Number, "0"));

	// The rotation.
	table.push_back(ServerVar("sv_randommaprotation", "Shuffle the rotation", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_maprotation", "Use the rotation", VarKind::Toggle, "1"));

	// What players may call a vote on. Each is a refusal, so the label says what it stops.
	table.push_back(ServerVar("sv_nomapvote", "Forbid map votes", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_nochangemapvote", "Forbid changemap votes", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_nonextmapvote", "Forbid nextmap votes", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_nofraglimitvote", "Forbid fraglimit votes", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_notimelimitvote", "Forbid timelimit votes", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_nopointlimitvote", "Forbid pointlimit votes", VarKind::Toggle, "0"));

	// The room itself.
	table.push_back(ServerVar("sv_maxclients", "Connection limit", VarKind::Number, "16"));
	table.push_back(ServerVar("sv_maxplayers", "Players in the game", VarKind::Number, "8"));
	table.push_back(ServerVar("sv_maxlives", "Lives", VarKind::Number, "0"));
	table.push_back(ServerVar("sv_maxteams", "Teams", VarKind::Number, "2"));

	// Spectators and the queue.
	table.push_back(ServerVar("sv_nospectatechat", "Silence spectators", VarKind::Toggle, "0"));
	table.push_back(ServerVar("sv_disallowvoting", "Forbid voting outright", VarKind::Toggle, "0"));

	return table;
}

ModeLimits LimitsForMode(bool earnsFrags, bool earnsPoints, bool earnsWins, bool usesLives,
	bool hasTeams)
{
	ModeLimits out;

	// [rc4l] Straight from what the mode declares. A mode can earn more than one thing -- Duel
	// earns frags AND wins -- and both limits are then real, so this is four independent questions
	// rather than one choice between four answers.
	out.fraglimit = earnsFrags;
	out.pointlimit = earnsPoints;
	out.winlimit = earnsWins;
	out.lives = usesLives;
	out.teams = hasTeams;

	return out;
}

bool ServerVarAccepts(VarKind kind, const std::string &text)
{
	// An empty box is somebody midway through typing, not an error.
	if (text.empty())
		return true;

	bool dot = false;

	for (size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];

		if ((c >= '0') && (c <= '9'))
			continue;

		// One decimal point, and only where a fraction is wanted.
		if ((c == '.') && (kind == VarKind::Fraction) && !dot)
		{
			dot = true;
			continue;
		}

		return false;
	}

	// A toggle is 0 or 1 and nothing else: anything further is a number somebody typed into the
	// wrong row, and taking it would set a switch to seven.
	if (kind == VarKind::Toggle)
		return (text == "0") || (text == "1");

	return true;
}

} // namespace zx
