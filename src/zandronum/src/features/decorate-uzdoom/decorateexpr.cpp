// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/decorate-uzdoom/decorateexpr.h"

#include <cstddef>
#include <vector>

#include "actor.h"
#include "actorptrselect.h"
#include "a_pickups.h"
#include "c_cvars.h"
#include "cmdlib.h"
#include "d_player.h"
#include "p_3dfloors.h"
#include "p_acs.h"
#include "p_local.h"
#include "p_pspr.h"
#include "r_defs.h"
#include "thingdef/thingdef.h"
#include "features/acs-parity/acsparity.h"
#include "features/decorate-uzdoom/computation/decoratemath_compute.h"

namespace zx { namespace decorateuzdoom {

namespace {

const unsigned kMaxQueryArgs = 5;

ExpVal IntVal(int v)
{
	ExpVal r;
	r.Type = VAL_Int;
	r.Int = v;
	return r;
}

ExpVal FloatVal(double v)
{
	ExpVal r;
	r.Type = VAL_Float;
	r.Float = v;
	return r;
}

double Degrees(angle_t a)
{
	return a * (360.0 / 4294967296.0);
}

// [Doruk-Aksoy] A class argument, given as a class or as a name looked up when the call runs.
const PClass *ClassOf(const ExpVal &v)
{
	const PClass *cls = v.GetClass();
	return cls != NULL ? cls : PClass::FindClass(v.GetName());
}

//==========================================================================
//
// The math builtins: one class for every entry of ComputeMathSpec's table.
//
//==========================================================================

class FxUZDoomMath : public FxGlobalFunctionCall
{
	MathSpec Spec;

public:
	FxUZDoomMath(FName name, FArgumentList *args, const FScriptPosition &pos, MathSpec spec)
	: FxGlobalFunctionCall(name, args, pos), Spec(spec)
	{
	}

	FxExpression *Resolve(FCompileContext &ctx)
	{
		CHECKRESOLVED();
		if (!ResolveArgs(ctx, Spec.minArgs, Spec.maxArgs, true))
			return NULL;
		bool allInt = true;
		for (unsigned i = 0; i < ArgList->Size(); i++)
		{
			if ((*ArgList)[i]->ValueType.Type != VAL_Int)
				allInt = false;
		}
		ValueType = ComputeMathResultIsInt(Spec.op, allInt) ? VAL_Int : VAL_Float;
		return this;
	}

	ExpVal EvalExpression(AActor *self)
	{
		std::vector<double> v(ArgList->Size());
		for (unsigned i = 0; i < ArgList->Size(); i++)
			v[i] = (*ArgList)[i]->EvalExpression(self).GetFloat();
		const double r = ComputeMath(Spec.op, v.data(), v.size());
		return ValueType.Type == VAL_Int ? IntVal(int(r)) : FloatVal(r);
	}
};

//==========================================================================
//
// The actor queries: one class for every entry of the table below.
//
//==========================================================================

// [Doruk-Aksoy] A query's arguments, those the call left out holding the query's defaults.
struct QueryArgs
{
	ExpVal v[kMaxQueryArgs];

	int Int(unsigned i) const { return v[i].GetInt(); }
	double Float(unsigned i) const { return v[i].GetFloat(); }
};

typedef ExpVal (*QueryEval)(AActor *self, const QueryArgs &a);

struct QuerySpec
{
	const char *name;
	unsigned minArgs, maxArgs;
	bool firstIsName;	// [Doruk-Aksoy] The first argument names a class or a cvar.
	ExpValType result;
	int defaults[kMaxQueryArgs];
	QueryEval eval;
};

ExpVal CountInv(AActor *self, const QueryArgs &a)
{
	AActor *owner = COPY_AAPTR(self, a.Int(1));
	const PClass *cls = ClassOf(a.v[0]);
	if (owner == NULL || cls == NULL || !cls->IsDescendantOf(RUNTIME_CLASS(AInventory)))
		return IntVal(0);
	AInventory *item = owner->FindInventory(cls);
	return IntVal(item != NULL ? item->Amount : 0);
}

ExpVal GetCVarValue(AActor *self, const QueryArgs &a)
{
	FBaseCVar *cvar = P_FindScriptCVar(self, a.v[0].GetName().GetChars());
	return FloatVal(cvar != NULL ? cvar->GetGenericRep(CVAR_Float).Float : 0);
}

ExpVal GetDistance(AActor *self, const QueryArgs &a)
{
	AActor *target = COPY_AAPTR(self, a.Int(1));
	if (target == NULL || target == self)
		return FloatVal(0);
	return FloatVal(ComputeGetDistance(FIXED2DBL(target->x - self->x), FIXED2DBL(target->y - self->y),
		FIXED2DBL(target->z - self->z), FIXED2DBL(self->height), FIXED2DBL(target->height), a.Int(0) != 0));
}

ExpVal GetAngle(AActor *self, const QueryArgs &a)
{
	AActor *target = COPY_AAPTR(self, a.Int(1));
	if (target == NULL || target == self)
		return FloatVal(0);
	const int flags = a.Int(0);
	AActor *from = (flags & kGafSwitch) ? target : self;
	AActor *to = (flags & kGafSwitch) ? self : target;
	return FloatVal(ComputeGetAngle(FIXED2DBL(from->x), FIXED2DBL(from->y), FIXED2DBL(to->x), FIXED2DBL(to->y),
		Degrees(from->angle), flags));
}

ExpVal GetZAt(AActor *self, const QueryArgs &a)
{
	AActor *mobj = COPY_AAPTR(self, a.Int(4));
	if (mobj == NULL)
		return FloatVal(0);
	const int flags = a.Int(3);
	double x, y;
	ComputeGetZAtPoint(FIXED2DBL(mobj->x), FIXED2DBL(mobj->y), Degrees(mobj->angle), a.Float(0), a.Float(1), a.Float(2), flags, &x, &y);
	const fixed_t fx = FLOAT2FIXED(x), fy = FLOAT2FIXED(y);
	sector_t *sec = P_PointInSector(fx, fy);
	if (sec == NULL)
		return FloatVal(0);
	const bool ceiling = (flags & kGzfCeiling) != 0;
	const double plane = FIXED2DBL(ceiling ? sec->ceilingplane.ZatPoint(fx, fy) : sec->floorplane.ZatPoint(fx, fy));
	if (flags & kGzfNo3DFloor)
		return FloatVal(plane);
	std::vector<Plane3D> floors;
	for (unsigned i = 0; i < sec->e->XFloor.ffloors.Size(); i++)
	{
		F3DFloor *ff = sec->e->XFloor.ffloors[i];
		Plane3D p = { FIXED2DBL(ff->top.plane->ZatPoint(fx, fy)), FIXED2DBL(ff->bottom.plane->ZatPoint(fx, fy)),
			(ff->flags & (FF_EXISTS | FF_SOLID)) == (FF_EXISTS | FF_SOLID) };
		floors.push_back(p);
	}
	const double z = FIXED2DBL(mobj->z);
	if (ceiling)
		return FloatVal(ComputeNextHighestCeiling(plane, floors.data(), floors.size(), z, z + FIXED2DBL(mobj->height)));
	return FloatVal(ComputeNextLowestFloor(plane, floors.data(), floors.size(), z, FIXED2DBL(mobj->MaxStepHeight),
		(flags & kGzf3DRestrict) != 0));
}

ExpVal GetSpawnHealth(AActor *self, const QueryArgs &)
{
	return IntVal(self->SpawnHealth());
}

ExpVal GetGibHealth(AActor *self, const QueryArgs &)
{
	return IntVal(self->GibHealth());
}

ExpVal GetMissileDamage(AActor *self, const QueryArgs &a)
{
	AActor *mobj = COPY_AAPTR(self, a.Int(2));
	return IntVal(mobj != NULL ? mobj->GetMissileDamage(a.Int(0), a.Int(1)) : 0);
}

ExpVal GetCrouchFactor(AActor *self, const QueryArgs &a)
{
	AActor *mobj = COPY_AAPTR(self, a.Int(0));
	return FloatVal(mobj != NULL && mobj->player != NULL ? FIXED2DBL(mobj->player->crouchfactor) : 1);
}

ExpVal CountProximity(AActor *self, const QueryArgs &a)
{
	return IntVal(zx::acsparity::Proximity(self, ClassOf(a.v[0]), a.Float(1), 0, a.Int(2), a.Int(3), true));
}

ExpVal GetPlayerInput(AActor *self, const QueryArgs &a)
{
	AActor *mobj = COPY_AAPTR(self, a.Int(1));
	return IntVal(mobj != NULL ? P_PlayerInputValue(mobj->player, a.Int(0)) : 0);
}

// [Doruk-Aksoy] A layer's offset on self's player, layer 0 being the one whose state runs, as UZDoom's GetOverlayPosition.
double OverlayPosition(AActor *self, int layer, bool y)
{
	if (self->player == NULL)
		return 0;
	if (layer == 0)
		layer = P_GetCurrentPSpriteLayer();
	const pspdef_t *psp = layer != 0 ? self->player->psprites.Find(layer) : NULL;
	return psp != NULL ? FIXED2DBL(y ? psp->sy : psp->sx) : 0;
}

ExpVal OverlayX(AActor *self, const QueryArgs &a)
{
	return FloatVal(OverlayPosition(self, a.Int(0), false));
}

ExpVal OverlayY(AActor *self, const QueryArgs &a)
{
	return FloatVal(OverlayPosition(self, a.Int(0), true));
}

// [Doruk-Aksoy] uzdoom@62afe5f0a9: actor.zs, compatibility.zs and inventory_util.zs, with each one's defaults.
const QuerySpec kQueries[] = {
	{ "CountInv", 1, 2, true, VAL_Int, { 0, AAPTR_DEFAULT }, CountInv },
	{ "GetCVar", 1, 1, true, VAL_Float, { 0 }, GetCVarValue },
	{ "GetDistance", 1, 2, false, VAL_Float, { 0, AAPTR_TARGET }, GetDistance },
	{ "GetAngle", 1, 2, false, VAL_Float, { 0, AAPTR_TARGET }, GetAngle },
	{ "GetZAt", 0, 5, false, VAL_Float, { 0, 0, 0, 0, AAPTR_DEFAULT }, GetZAt },
	{ "GetSpawnHealth", 0, 0, false, VAL_Int, { 0 }, GetSpawnHealth },
	{ "GetGibHealth", 0, 0, false, VAL_Int, { 0 }, GetGibHealth },
	{ "GetMissileDamage", 2, 3, false, VAL_Int, { 0, 0, AAPTR_DEFAULT }, GetMissileDamage },
	{ "GetCrouchFactor", 0, 1, false, VAL_Float, { AAPTR_PLAYER1 }, GetCrouchFactor },
	{ "CountProximity", 2, 4, true, VAL_Int, { 0, 0, 0, AAPTR_DEFAULT }, CountProximity },
	{ "GetPlayerInput", 1, 2, false, VAL_Int, { 0, AAPTR_DEFAULT }, GetPlayerInput },
	{ "OverlayX", 0, 1, false, VAL_Float, { 0 }, OverlayX },
	{ "OverlayY", 0, 1, false, VAL_Float, { 0 }, OverlayY },
};

const QuerySpec *FindQuery(FName name)
{
	for (const QuerySpec &q : kQueries)
	{
		if (stricmp(name.GetChars(), q.name) == 0)
			return &q;
	}
	return NULL;
}

class FxUZDoomQuery : public FxGlobalFunctionCall
{
	const QuerySpec *Spec;

public:
	FxUZDoomQuery(FName name, FArgumentList *args, const FScriptPosition &pos, const QuerySpec *spec)
	: FxGlobalFunctionCall(name, args, pos), Spec(spec)
	{
	}

	FxExpression *Resolve(FCompileContext &ctx)
	{
		CHECKRESOLVED();
		if (!ResolveArgs(ctx, Spec->minArgs, Spec->maxArgs, false))
			return NULL;
		const unsigned count = ArgList != NULL ? ArgList->Size() : 0;
		for (unsigned i = 0; i < count; i++)
		{
			const int t = (*ArgList)[i]->ValueType.Type;
			const bool named = i == 0 && Spec->firstIsName;
			if (named ? (t != VAL_Name && t != VAL_Class) : !(*ArgList)[i]->ValueType.isNumeric())
			{
				ScriptPosition.Message(MSG_ERROR, named ? "%s expects a name for its first parameter" : "%s expects a number for parameter %u",
					Spec->name, i + 1);
				delete this;
				return NULL;
			}
		}
		ValueType = Spec->result;
		return this;
	}

	ExpVal EvalExpression(AActor *self)
	{
		QueryArgs a;
		for (unsigned i = 0; i < kMaxQueryArgs; i++)
			a.v[i] = IntVal(Spec->defaults[i]);
		const unsigned count = ArgList != NULL ? ArgList->Size() : 0;
		for (unsigned i = 0; i < count; i++)
			a.v[i] = (*ArgList)[i]->EvalExpression(self);
		if (self == NULL)
			return Spec->result == VAL_Int ? IntVal(0) : FloatVal(0);
		return Spec->eval(self, a);
	}
};

} // namespace

FxExpression *CreateGlobalCall(FName name, FArgumentList *args, const FScriptPosition &pos)
{
	const MathSpec math = ComputeMathSpec(name.GetChars());
	if (math.op != kMathNone)
		return new FxUZDoomMath(name, args, pos, math);
	if (const QuerySpec *q = FindQuery(name))
		return new FxUZDoomQuery(name, args, pos, q);
	return NULL;
}

}} // namespace zx::decorateuzdoom
