// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-fields/acsfields.h"

#include "actor.h"
#include "dobjtype.h"
#include "name.h"
#include "thingdef/thingdef_type.h"
#include "features/actor-layout/computation/actorext_compute.h"   // [Doruk-Aksoy] ComputeIsExtMember

static_assert(zx::acsfields::kValInt == VAL_Int && zx::acsfields::kValFloat == VAL_Float && zx::acsfields::kValArray == VAL_Array
	&& zx::acsfields::kValFixed == VAL_Fixed && zx::acsfields::kValAngle == VAL_Angle && zx::acsfields::kValBool == VAL_Bool, "DECORATE's value types keep their numbers");

namespace zx { namespace acsfields {

namespace {

// [Doruk-Aksoy] The field's shape and where it sits in the actor, through the symbol table DECORATE fills; kFieldNone when no variable has the name.
FieldShape Find(AActor *actor, const char *name, unsigned char *&address)
{
	address = nullptr;
	const FName field(name != NULL ? name : "", true);
	if (actor == NULL || field == NAME_None)
		return FieldShape{ kFieldNone, 0, false };
	PSymbol *sym = actor->GetClass()->Symbols.FindSymbol(field, true);
	if (sym == NULL || sym->SymbolType != SYM_Variable)
		return FieldShape{ kFieldNone, 0, false };
	const PSymbolVariable *var = static_cast<PSymbolVariable *>(sym);
	// [Doruk-Aksoy] A native kept in the rare-field block (features/actor-layout) is read in the block the actor reads; writes reach user_ variables only.
	if (zx::actorlayout::ComputeIsExtMember(var->offset))
		address = const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(&actor->Ext())) + zx::actorlayout::ComputeExtMemberOffset(var->offset);
	else
		address = reinterpret_cast<unsigned char *>(actor) + var->offset;
	return ComputeFieldShape(var->ValueType.Type, var->ValueType.BaseType, var->ValueType.size, var->bUserVar);
}

} // namespace

int32_t GetField(AActor *actor, const char *name, int index)
{
	unsigned char *address;
	const FieldShape shape = Find(actor, name, address);
	return ComputeFieldRead(shape, address, index);
}

bool SetField(AActor *actor, const char *name, int index, int32_t value)
{
	unsigned char *address;
	const FieldShape shape = Find(actor, name, address);
	return ComputeFieldWrite(shape, address, index, value);
}

}} // namespace zx::acsfields
