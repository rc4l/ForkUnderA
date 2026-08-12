// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How many teams, and whether that is even a question.
//
// This replaces three remix folders -- ffa, teamdm and teamlms -- which between them could only ever
// say "teams" or "no teams", and said it with two pills whose labels claimed to be gamemodes. The
// axis is really a NUMBER: Zandronum carries sv_maxteams and TEAMINFO defines four, so a deathmatch
// can be run as a free-for-all or as two, three or four sides.
//
// The stops are 0, 2, 3 and 4 with no 1 between them, because one team is not a thing anybody can
// mean: it is a free-for-all where the frags happen to share a colour. The gap is why the control
// runs on a stop INDEX rather than the count itself.
//
// Three gamemodes have a team twin to switch to:
//
//   * DEATHMATCH becomes teamplay.
//   * LAST MAN STANDING becomes teamlms.
//   * POSSESSION becomes teampossession.
//
// What they share is that the thing being fought over is spawned by the ENGINE rather than placed
// by the mapper, so any number of sides can play on a map built for none of them. Terminator has no
// twin at all: there is one ball and whoever holds it is everyone's enemy, which is the mode.
//
// Everything else is left alone. Not for want of a cvar -- CTF and Skulltag honour sv_maxteams
// perfectly well -- but because their maps do not: a CTF map carries two flags, and a third team
// would spawn with nothing to take and nothing to lose. Modes whose teams come from the map are the
// map's business, and they are told so in those words rather than the generic refusal, because an
// author who reads "no free-for-all to switch out of" about CTF will think we have it wrong.
//
// Which is also why the control is opt-in per entry rather than read off the gamemode alone. The
// Skulltag entry has a variant that declares deathmatch and then exec's a cfg naming the Skulltag
// gamemode; inferring from the mode would hand it a control whose "no teams" stop would quietly
// switch the whole thing back to plain deathmatch.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_TEAMSPICK_COMPUTE_H
#define ZX_TEAMSPICK_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

#include "features/addon-catalogue/computation/addonfile_compute.h"

namespace zx
{

enum class TeamsShape
{
	// No control. Either the entry never offered one, or the gamemode has no team twin to switch to.
	None,

	// A free-for-all that can become teams, and back. The zero stop changes the gamemode.
	Optional,
};

struct TeamsControl
{
	TeamsShape shape;

	// Whether teams mean anything here, which is what decides whether the cvars get written. A mode
	// with no twin must not have one named, or the panel is claiming something the server will not do.
	bool applies;

	// Whether there is anything to choose. Always true alongside `applies` today, and kept separate
	// for the same reason LivesControl keeps it: the caller draws on this and sets cvars on the other,
	// so pinning the count later is a change to this file and nowhere else.
	bool adjustable;

	int count;			// 0 for a free-for-all, otherwise 2 to 4
	int stop;			// `count`'s place in the stop list, which is what the slider moves

	// The gamemode cvars this axis switches between. Named here rather than in the caller so the
	// mapping lives with the reasoning above it.
	std::string soloCvar;
	std::string teamCvar;

	std::string reason;	// why there is no control; empty unless `applies` is false

	TeamsControl() : shape(TeamsShape::None), applies(false), adjustable(false), count(0), stop(0) {}
};

// How many stops the slider has. A function rather than a constant because a namespace-scope
// `inline constexpr` is C++17 and this tree is C++14.
int TeamsStopCount();

// The team count at a stop, and the stop for a count. Out-of-range stops give the nearest end, and
// any count below two is the free-for-all: one team is a free-for-all wearing a colour, and a stored
// 1 from anywhere else should land there rather than be rounded up into a real team.
int TeamsCountAtStop(int stop);
int TeamsStopForCount(int count);

// `offered` is the entry's own say-so. `wanted` is what the player last asked for, which may be left
// over from a different experience; a NEGATIVE means nothing chosen, which is a free-for-all.
TeamsControl TeamsFor(HostGameMode mode, bool offered, int wanted);

// The cvars that put `control` into effect, in the order they must be set. Both sides of the switch
// are written every time rather than only the one that changed, because the experience's own cfg has
// already named a gamemode and a control that can only turn teams on cannot turn them off again.
std::vector<std::pair<std::string, std::string> > TeamsCvars(const TeamsControl &control);

} // namespace zx

#endif // ZX_TEAMSPICK_COMPUTE_H
