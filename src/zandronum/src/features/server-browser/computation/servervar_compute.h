// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] The server settings that are NOT flags, and which of them a gamemode actually uses.
//
// Two separate jobs that share a table, because they are the same question asked twice: "what can
// this server be told" and "what is worth showing for the mode it is running".
//
// WHY A TABLE AND NOT A WALK. The flags could be read straight off the engine -- an FFlagCVar knows
// its field and its bit, so the list builds itself. These cannot: there is no marker on the cvar
// saying "a person hosting a server would want to set this", and CVAR_SERVERINFO alone pulls in a
// hundred things nobody sets by hand. So this list is chosen, and being chosen it is written down
// where it can be read and argued with rather than inferred.
//
// The seed is what OUR OWN catalogue cfgs actually set across every experience we ship. That is a
// real sample of what hosting a Doom server needs rather than a guess at it.
//
// CONTEXT COMES FROM THE ENGINE, NOT FROM HERE. Which limit a mode uses is already declared in
// gamemode.txt -- PLAYERSEARNFRAGS, PLAYERSEARNPOINTS, PLAYERSEARNWINS, USEMAXLIVES -- and the
// engine parses it. So the caller passes those capability bits in and this decides what to show
// from them. Duel earning both frags and wins is why it shows a win limit, and that falls out of
// the table rather than needing a rule of its own.
//
// Header-pure by the features/ rules, no engine types.

#ifndef ZX_SERVERVAR_COMPUTE_H
#define ZX_SERVERVAR_COMPUTE_H

#include <string>
#include <vector>

namespace zx
{

enum class VarKind
{
	Toggle,		// 0 or 1
	Number,		// a whole number
	Fraction,	// 0.0 to 1.0, like teamdamage
};

struct ServerVar
{
	std::string name;		// the cvar
	std::string label;		// what the row says
	VarKind kind;
	std::string fallback;	// what it is when nobody has said otherwise

	ServerVar() : kind(VarKind::Toggle) {}
	ServerVar(const std::string &n, const std::string &l, VarKind k, const std::string &d)
		: name(n), label(l), kind(k), fallback(d) {}
};

// The settings the VARIABLES panel offers, in the order it shows them.
const std::vector<ServerVar> &ServerVarTable();

// [rc4l] What a gamemode's capability bits mean for the limits.
//
// The bits are the engine's own GMF_* values, passed in rather than looked up, so this stays
// testable without an engine. Only the four that decide a limit are named.
struct ModeLimits
{
	bool fraglimit;
	bool pointlimit;
	bool winlimit;
	bool lives;
	bool teams;

	ModeLimits() : fraglimit(false), pointlimit(false), winlimit(false), lives(false), teams(false) {}
};

// `earnsFrags`, `earnsPoints`, `earnsWins` and `usesLives` come from the mode's declared flags.
// `hasTeams` is whether the mode plays in sides at all.
ModeLimits LimitsForMode(bool earnsFrags, bool earnsPoints, bool earnsWins, bool usesLives,
	bool hasTeams);

// Whether a value is a plausible answer for a setting of this kind. Refusing at the keystroke says
// so where it happens rather than when the server fails to start.
bool ServerVarAccepts(VarKind kind, const std::string &text);

} // namespace zx

#endif // ZX_SERVERVAR_COMPUTE_H
