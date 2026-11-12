// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which script ACS's CheckScript asks about.

#ifndef ZX_CHECKSCRIPT_COMPUTE_H
#define ZX_CHECKSCRIPT_COMPUTE_H

namespace zx {

// [rc4l] Adapted from zandronum@92ebf777950155f28651186233b53d682acd986e: by name only when a second argument says so, since with one argument the slot past it is stale stack.
bool ComputeCheckScriptByName(int argCount, int byNameArg);

// [rc4l] The script number CheckScript looks up: a named script's is the negative of its name's index, as named scripts are numbered.
int ComputeCheckScriptNumber(bool byName, int number, int nameIndex);

} // namespace zx

#endif
