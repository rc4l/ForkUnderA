// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

// [rc4l] Every server flag this build has, asked of the engine rather than written out again.
//
// A flag is an FFlagCVar: a name, the integer cvar its bit lives in, and the bit. All three are
// already on the object, and every cvar is on one list, so the whole table is a walk. Nothing is
// parsed, nothing is generated, and nothing can drift -- a flag added anywhere in the tree appears
// here the moment it is declared.
//
// THAT MATTERS MORE THAN IT SOUNDS. The obvious way to build this table is to read d_main.cpp,
// where the flags plainly are: 146 of them across six fields. It would also be wrong. sv_nounlagged
// is declared in unlagged.cpp and lms_spectatorchat in lastmanstanding.cpp -- and that second one
// belongs to a SEVENTH bitfield, lmsspectatorsettings, which no list of "the six dmflags fields"
// would have mentioned. A table written by hand or scraped from one file would have been quietly
// short, and short in a way nobody would notice until a flag did nothing.

#ifndef ZX_FLAGTABLE_H
#define ZX_FLAGTABLE_H

#include <string>
#include <vector>

#include "features/server-browser/computation/flagset_compute.h"

namespace zx
{

// Every field that has named bits, in the order FlagFieldOrder gives, each with its bits sorted low
// to high and its `value` filled in from the cvar as it stands right now.
//
// Rebuilt on each call, which is cheap -- a few hundred cvars -- and is what makes the answer
// current rather than a snapshot from whenever the screen was opened.
std::vector<FlagField> FlagTable();

// Write a field back to the engine. The cvar is set by name, so the callbacks Zandronum hangs off
// these -- the ones that keep dmflags and its friends in step with the game state -- all run.
void FlagTableApply(const std::string &field, unsigned int value);

} // namespace zx

#endif // ZX_FLAGTABLE_H
