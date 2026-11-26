// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/decorate-uzdoom/uservars.h"

#include "dobject.h"
#include "dobjtype.h"
#include "thingdef/thingdef.h"
#include "thingdef/thingdef_exp.h"
#include "features/decorate-uzdoom/computation/uservar_compute.h"

namespace zx { namespace decorateuzdoom {

bool IsFloatUserVar(const PSymbolVariable *var)
{
	const int type = var->ValueType.Type == VAL_Array ? var->ValueType.BaseType : var->ValueType.Type;
	return type == VAL_Float;
}

unsigned UserVarCount(const PSymbolVariable *var)
{
	return var->ValueType.Type == VAL_Array ? var->ValueType.size : 1;
}

UserVarSlot FindUserVar(DObject *self, FName name, UserVarShape shape, int index)
{
	UserVarSlot slot = { NULL, false, kUserVarNotDeclared };
	PSymbol *sym = self->GetClass()->Symbols.FindSymbol(name, true);
	if (sym == NULL || sym->SymbolType != SYM_Variable)
		return slot;
	PSymbolVariable *var = static_cast<PSymbolVariable *>(sym);
	const bool array = var->ValueType.Type == VAL_Array;
	const int element = array ? var->ValueType.BaseType : var->ValueType.Type;
	slot.miss = ComputeUserVarAccess(var->bUserVar && (element == VAL_Int || element == VAL_Float), array, UserVarCount(var), shape, index);
	if (slot.miss != kUserVarFound)
		return slot;
	slot.isFloat = element == VAL_Float;
	slot.address = reinterpret_cast<char *>(self) + var->offset + index * (slot.isFloat ? sizeof(double) : sizeof(int));
	return slot;
}

void StoreUserVar(const UserVarSlot &slot, int value)
{
	if (slot.isFloat)
		*reinterpret_cast<double *>(slot.address) = value;
	else
		*reinterpret_cast<int *>(slot.address) = value;
}

void StoreUserVar(const UserVarSlot &slot, double value)
{
	if (slot.isFloat)
		*reinterpret_cast<double *>(slot.address) = value;
	else
		*reinterpret_cast<int *>(slot.address) = ComputeUserVarIntFromDouble(value);
}

double LoadUserVar(const UserVarSlot &slot)
{
	return slot.isFloat ? *reinterpret_cast<const double *>(slot.address) : *reinterpret_cast<const int *>(slot.address);
}

}} // namespace zx::decorateuzdoom
