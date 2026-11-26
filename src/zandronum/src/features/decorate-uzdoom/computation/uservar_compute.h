// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_USERVAR_COMPUTE_H
#define ZX_USERVAR_COMPUTE_H

#include <cstdint>

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] Which shapes of user variable a lookup takes: a single value, an array, or either.
enum UserVarShape
{
	kUserVarScalar,
	kUserVarArray,
	kUserVarEither,
};

// [Doruk-Aksoy] Whether a lookup found an element to read or write, and why not.
enum UserVarMiss
{
	kUserVarFound,
	kUserVarNotDeclared,
	kUserVarOutOfBounds,
};

// [Doruk-Aksoy] A lookup's outcome for a user variable of int or float elements, count of them, in the shapes allowed.
UserVarMiss ComputeUserVarAccess(bool numericUserVar, bool isArray, unsigned count, UserVarShape shape, int index);

// [Doruk-Aksoy] Where a user variable of count elements goes on a class of classSize bytes: its offset and how much the class grows.
struct UserVarLayout
{
	unsigned offset;
	unsigned extension;
};

// [Doruk-Aksoy] Ints take 4 bytes each, floats are doubles of 8 placed on an 8-byte boundary, as UZDoom's are.
UserVarLayout ComputeUserVarLayout(unsigned classSize, bool isFloat, unsigned count);

// [Doruk-Aksoy] A double stored into an int user variable, truncated toward zero and held inside int's range.
int32_t ComputeUserVarIntFromDouble(double value);

// [Doruk-Aksoy] ACS's 16.16 for a float user variable's value, rounded half to even as UZDoom's DoubleToACS.
int32_t ComputeUserVarToAcs(double value);

// [Doruk-Aksoy] A float user variable's value from ACS's 16.16, as UZDoom's ACSToDouble.
double ComputeUserVarFromAcs(int32_t value);

}} // namespace zx::decorateuzdoom

#endif // ZX_USERVAR_COMPUTE_H
