// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_USERVARS_H
#define ZX_USERVARS_H

#include "name.h"
#include "features/decorate-uzdoom/computation/uservar_compute.h"

class DObject;
struct PSymbolVariable;

namespace zx { namespace decorateuzdoom {

// [Doruk-Aksoy] One element of a user variable: where it is and whether it holds a double.
struct UserVarSlot
{
	char *address;
	bool isFloat;
	UserVarMiss miss;
};

// [Doruk-Aksoy] Whether a symbol is a float user variable or an array of them.
bool IsFloatUserVar(const PSymbolVariable *var);

// [Doruk-Aksoy] How many elements a user variable holds.
unsigned UserVarCount(const PSymbolVariable *var);

// [Doruk-Aksoy] Element index of the user variable name on self, in the shapes allowed.
UserVarSlot FindUserVar(DObject *self, FName name, UserVarShape shape, int index);

// [Doruk-Aksoy] Writes into an element, an int one truncating a double as UZDoom's SetValue does.
void StoreUserVar(const UserVarSlot &slot, int value);
void StoreUserVar(const UserVarSlot &slot, double value);

// [Doruk-Aksoy] Reads an element as a double.
double LoadUserVar(const UserVarSlot &slot);

}} // namespace zx::decorateuzdoom

#endif // ZX_USERVARS_H
