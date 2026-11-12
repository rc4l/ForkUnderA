// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/checkscript_compute.h"

namespace zx {

bool ComputeCheckScriptByName(int argCount, int byNameArg)
{
	return argCount > 1 && byNameArg != 0;
}

int ComputeCheckScriptNumber(bool byName, int number, int nameIndex)
{
	return byName ? -nameIndex : number;
}

} // namespace zx
