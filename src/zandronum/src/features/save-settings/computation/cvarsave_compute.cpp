// [MobiusU9B] See cvarsave_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MobiusU9B
#include "features/save-settings/computation/cvarsave_compute.h"

#include <cstring>

namespace zx {

namespace {

// [MobiusU9B] A cvar's string with NULL read as "", since the engine keeps an empty saved value as NULL.
const char *OrEmpty(const char *text)
{
	return text != nullptr ? text : "";
}

// [MobiusU9B] Whether two cvar strings differ, NULL counting as "".
bool Differs(const char *a, const char *b)
{
	return std::strcmp(OrEmpty(a), OrEmpty(b)) != 0;
}

} // namespace

bool ComputeRecordsPreScriptValue(bool engineCVar, bool alreadyRecorded, const char *current, const char *next)
{
	return engineCVar && !alreadyRecorded && Differs(current, next);
}

const char *ComputeArchivedCVarValue(bool protect, const char *live, bool recorded, const char *original)
{
	return OrEmpty((protect && recorded) ? original : live);
}

bool ComputeResetChangesCVar(const char *live, const char *saved)
{
	return Differs(live, saved);
}

bool ComputeSendsUserCVarChange(bool isServer, const char *before, const char *after)
{
	return isServer && Differs(before, after);
}

bool ComputeMirrorsToOwnCVar(bool isConsolePlayer, bool playingDemo, bool modUserCVar, bool ignored, const char *current, const char *incoming)
{
	return isConsolePlayer && !playingDemo && modUserCVar && !ignored && Differs(current, incoming);
}

} // namespace zx
