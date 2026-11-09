// [MobiusU9B] Which cvar value a config save keeps, and how a mod's user cvar set on the server reaches the client that saves it, kept engine-free so it is unit-tested.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#ifndef ZX_CVARSAVE_COMPUTE_H
#define ZX_CVARSAVE_COMPUTE_H

namespace zx {

// [MobiusU9B] cl_protectcvars records an engine cvar's value before a script's ConsoleCommand changes it, once, and only when the value really changes; a mod's own cvars are never recorded.
bool ComputeRecordsPreScriptValue(bool engineCVar, bool alreadyRecorded, const char *current, const char *next);

// [MobiusU9B] The value a save writes: the recorded pre-script value while cl_protectcvars guards the cvar, otherwise the live one, with NULL read as "".
const char *ComputeArchivedCVarValue(bool protect, const char *live, bool recorded, const char *original);

// [MobiusU9B] Reset to last saved sets a cvar only when its saved value differs from its live one, since a set runs the cvar's callback even for the same value.
bool ComputeResetChangesCVar(const char *live, const char *saved);

// [MobiusU9B] A server tells the clients when a script changed a player's mod user cvar, and only when its value changed, so a script setting it every tic sends nothing.
bool ComputeSendsUserCVarChange(bool isServer, const char *before, const char *after);

// [MobiusU9B] A client copies the server's value of its own player's mod user cvar into its own cvar, the one the ini saves, unless it plays a demo, the cvar is ignored, or nothing changed.
bool ComputeMirrorsToOwnCVar(bool isConsolePlayer, bool playingDemo, bool modUserCVar, bool ignored, const char *current, const char *incoming);

} // namespace zx

#endif
