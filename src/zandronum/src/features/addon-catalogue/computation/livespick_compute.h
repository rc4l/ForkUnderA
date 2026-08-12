// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] How many lives a player gets, and whether that is even a question.
//
// It is not one axis, which is why three interchangeable remix folders could never express it. Only
// four gamemodes honour sv_maxlives at all -- Survival, Invasion, Last Man Standing and Team Last
// Man Standing, per wadsrc/static/gamemode.txt -- and the cvar does not mean the same thing in all
// of them:
//
//   * In INVASION, sv_maxlives 0 is genuinely unlimited. GAMEMODE_AreLivesLimited says so as an
//     explicit special case.
//   * In Survival and both LMS modes it is NOT. GAMEMODE_GetMaxLives returns 1 for a zero, so the
//     lowest meaningful value is one life and there is no unlimited at all.
//   * In plain COOPERATIVE the cvar is ignored outright, because Cooperative does not carry the
//     flag. Unlimited lives in co-op is not a lives value, it is the Cooperative gamemode; asking
//     for lives means switching to Survival.
//
// That last one shipped as a bug. Three cooperative entries offered a "Survival" remix that set
// sv_maxlives and nothing else, so it did nothing at all. A control that reads the gamemode cannot
// make that mistake, which is the reason this exists rather than a fourth remix folder.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_LIVESPICK_COMPUTE_H
#define ZX_LIVESPICK_COMPUTE_H

#include <string>
#include <utility>
#include <vector>

#include "features/addon-catalogue/computation/addonfile_compute.h"

namespace zx
{

enum class LivesShape
{
	// The gamemode has no notion of lives. The control is drawn inert rather than hidden: a row that
	// vanishes teaches nothing, and the reader is entitled to know why it cannot be used.
	None,

	// Zero is the COOPERATIVE gamemode and anything above it is Survival with that many lives. The
	// zero stop changes the mode, not a number.
	CoopSurvival,

	// Zero really is unlimited, and the mode never changes.
	Invasion,

	// Survival, LMS or Team LMS declared outright. One life is the floor; there is no unlimited.
	Rounds,
};

struct LivesControl
{
	LivesShape shape;

	// Whether lives mean anything here at all, which is what decides whether the cvars get set. A
	// gamemode that ignores sv_maxlives must not have it written, or the panel is claiming something
	// the server will not do.
	bool applies;

	// Whether there is anything to CHOOSE: more than one stop. Separate from `applies` because an
	// entry can pin its lives -- Super Demon is one life a round and says so with a ceiling of one --
	// and a slider with a single stop is a readout wearing a control's clothes, spending two lines of
	// a short column to say what the summary already said.
	//
	// The caller draws only when this is true, and sets the cvars whenever `applies` is.
	bool adjustable;

	int min;			// lowest selectable value; 0 exactly when an unlimited stop exists
	int max;			// highest selectable
	int value;			// what is in force, always within [min, max]

	// True when `value` means "no limit" rather than a count. Only ever true at zero, and only in
	// the two shapes that have such a stop.
	bool unlimited;

	std::string reason;	// why it cannot be used; empty unless `applies` is false

	LivesControl() : shape(LivesShape::None), applies(false), adjustable(false), min(0), max(0),
		value(0), unlimited(false) {}
};

// `wanted` is what the player last asked for, which may be out of range for this gamemode or left
// over from a different one. It is clamped rather than refused: the axis is a number, and a number
// that no longer fits has an obvious nearest answer.
//
// Any NEGATIVE `wanted` means "nothing chosen", which takes the entry's own default. One rule
// rather than a special case for -1: a negative life count is not something anybody can have meant,
// so reading them all the same way beats clamping some of them to zero and turning a stored -3 into
// a request for unlimited.
//
// `defaultLives` and `maxLives` come from the entry. Bosses from Hell wants unlimited and says so;
// a boss rush at one life is a different game.
LivesControl LivesFor(HostGameMode mode, int wanted, int defaultLives, int maxLives);

// The cvars that put `control` into effect, in the order they must be set.
//
// Emitted directly rather than through a shared cfg, which is the other half of why the bug was
// unfixable before: one survival.cfg exec'd by both a cooperative entry and an invasion one has to
// mean two different things, and a file cannot know which it landed in.
std::vector<std::pair<std::string, std::string> > LivesCvars(const LivesControl &control);

} // namespace zx

#endif // ZX_LIVESPICK_COMPUTE_H
