// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSARGCOUNT_COMPUTE_H
#define ZX_ACSARGCOUNT_COMPUTE_H

namespace zx { namespace acsargcount {

// [Doruk-Aksoy] An ACS function by its CALLFUNC index, the arguments it reads without checking how many a script passed, and its name for the message.
struct FunctionArgs
{
	int index;
	int needed;
	const char *name;
};

// [Doruk-Aksoy] Every function CallFunction runs that reads an argument unchecked, ascending by index.
extern const FunctionArgs kFunctionArgs[];
extern const int kFunctionArgsCount;

// [Doruk-Aksoy] The function's entry, or nullptr when it checks every argument it reads.
const FunctionArgs *ComputeFunctionArgs(int funcIndex);

// [Doruk-Aksoy] The entry of a function called with fewer arguments than it reads, or nullptr when the call passes enough.
const FunctionArgs *ComputeShortCall(int funcIndex, int argCount);

}} // namespace zx::acsargcount

#endif // ZX_ACSARGCOUNT_COMPUTE_H
