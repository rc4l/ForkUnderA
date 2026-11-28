// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-parity/acsparity.h"

#include "actor.h"
#include "c_console.h"
#include "actorptrselect.h"
#include "d_player.h"
#include "g_shared/a_pickups.h"
#include "features/net-session/netrole.h"
#include "p_3dfloors.h"
#include "p_local.h"
#include "p_tags.h"
#include "p_terrain.h"
#include "r_state.h"
#include "features/actor-flags/actorflags.h"
#include "features/trnslate/trnslate.h"

namespace zx { namespace acsparity {

namespace {

double ToMapUnits(fixed_t f)
{
	return f.Raw() / 65536.0;
}

ProximityPoint PointOf(const AActor* actor)
{
	const double z = ToMapUnits(actor->z);
	return { ToMapUnits(actor->x), ToMapUnits(actor->y), z, z + ToMapUnits(actor->height) };
}

} // namespace

int InventoryAmount(AActor* actor, const char* type, bool max)
{
	if (actor == nullptr || type == nullptr)
		return 0;
	const InventoryKind kind = ComputeInventoryKind(type);
	if (kind == kInventoryHealth)
	{
		if (!max)
			return actor->health;
		return actor->IsKindOf(RUNTIME_CLASS(APlayerPawn)) ? static_cast<APlayerPawn*>(actor)->GetMaxHealth() : actor->SpawnHealth();
	}
	const PClass* info = PClass::FindClass(kind == kInventoryArmor ? "BasicArmor" : type);
	const AInventory* item = actor->FindInventory(info);
	InventoryState state = { item != nullptr, 0, 0, false, 0 };
	if (item != nullptr)
	{
		state.amount = item->Amount;
		state.maxAmount = item->MaxAmount;
	}
	else if (info != nullptr && info->IsDescendantOf(RUNTIME_CLASS(AInventory)))
	{
		state.isInventory = true;
		state.defaultMax = static_cast<AInventory*>(GetDefaultByType(info))->MaxAmount;
	}
	return ComputeInventoryAmount(state, max);
}

int CheckProximity(AActor* self, const char* className, int32_t distance, int count, int flags, int ptr)
{
	return Proximity(self, className != nullptr ? PClass::FindClass(className) : nullptr, distance / 65536.0, count, flags, ptr, false);
}

int Proximity(AActor* self, const PClass* cls, double distance, int count, int flags, int ptr, bool counting)
{
	AActor* ref = COPY_AAPTR(self, ptr);
	if (ref == nullptr || cls == nullptr)
		return 0;
	TThinkerIterator<AActor> it;
	const auto next = [&](AActor*& handle, ProximityPoint& point, bool& killed) {
		for (AActor* mo = it.Next(); mo != nullptr; mo = it.Next())
		{
			if (mo == ref || (mo->flags & MF_UNMORPHED))
				continue;
			if ((flags & kCpxAncestor) ? !mo->IsKindOf(cls) : mo->GetClass() != cls)
				continue;
			// [Doruk-Aksoy] A carried item keeps the spot it was picked up from, so UZDoom skips it.
			if (mo->IsKindOf(RUNTIME_CLASS(AInventory)) && static_cast<AInventory*>(mo)->Owner != nullptr)
				continue;
			handle = mo;
			point = PointOf(mo);
			killed = (mo->flags6 & MF6_KILLED) != 0;
			return true;
		}
		return false;
	};
	const auto sight = [ref](AActor* mo) { return P_CheckSight(mo, ref, SF_IGNOREVISIBILITY | SF_IGNOREWATERBOUNDARY); };
	const ProximityScan<AActor*> scan = ComputeProximityScan<AActor*>(next, sight, PointOf(ref), distance, count, flags, counting);
	if (scan.chosen != nullptr)
	{
		AActor* owner = (flags & kCpxSetOnPtr) ? ref : self;
		if (flags & kCpxSetTarget)
			owner->target = scan.chosen;
		if (flags & kCpxSetMaster)
			owner->master = scan.chosen;
		if (flags & kCpxSetTracer)
			owner->tracer = scan.chosen;
	}
	return scan.result;
}

int32_t LineCoord(int lineId, int axis, int32_t along, int32_t offset)
{
	FLineIdIterator it(lineId);
	const int lineno = it.Next();
	if (lineno < 0)
		return 0;
	const line_t& line = lines[lineno];
	const double result = ComputeLineCoord(axis, ToMapUnits(line.v1->x), ToMapUnits(line.v1->y), ToMapUnits(line.dx), ToMapUnits(line.dy), along / 65536.0, offset / 65536.0);
	return ComputeFixedFromDouble(result);
}

AActor* NthActorFromTID(int tid, int index, AActor* defactor)
{
	if (tid == 0)
		return defactor;
	FActorIterator it(tid);
	return ComputeNth([&it]() { return it.Next(); }, index);
}

AActor* ActorFromNetID(int32_t id)
{
	return ComputeNetIdValid(id) ? g_ActorNetIDList.findPointerByID(static_cast<unsigned short>(id)) : nullptr;
}

const char* FloorTerrainName(AActor* actor)
{
	return Terrains[P_GetThingFloorType(actor)].Name.GetChars();
}

void SetSectorDamage(int tag, int32_t amount, const char* type)
{
	int mod = ComputeDamageTypeMod(type);
	if (mod < 0)
	{
		// [Doruk-Aksoy] Each name warns once, so a script setting it every tic doesn't flood the console.
		static TArray<FName> warned;
		const FName name(type);
		if (warned.Find(name) == warned.Size())
		{
			warned.Push(name);
			Printf(TEXTCOLOR_ORANGE "SetSectorDamage: sectors here hold only the built-in damage types, so '%s' does normal damage.\n", type);
		}
		mod = 0;
	}
	const short damage = ComputeSectorDamageAmount(amount);
	FSectorTagIterator it(tag);
	int s;
	while ((s = it.Next()) >= 0)
	{
		sectors[s].damage = damage;
		sectors[s].mod = short(mod);
	}
}

bool IsActorClass(const char* name)
{
	const PClass* cls = name != nullptr ? PClass::FindClass(name) : nullptr;
	return cls != nullptr && cls->IsDescendantOf(RUNTIME_CLASS(AActor));
}

int SetActorFlag(AActor* activator, int tid, const char* name, bool on)
{
	// [Doruk-Aksoy] On a client only a clientside script runs, and its changes stay on its machine.
	const bool local = zx::net::InClientMode();
	if (tid == 0)
		return actorflags::ChangeActorFlag(activator, name, on, true, local) ? 1 : 0;
	// [Doruk-Aksoy] Actors sharing a tid needn't share the flag, so none of them reports a miss, as UZDoom's doesn't.
	int count = 0;
	FActorIterator it(tid);
	while (AActor* actor = it.Next())
		count += actorflags::ChangeActorFlag(actor, name, on, false, local) ? 1 : 0;
	return count;
}

void SetTranslation(AActor* activator, int tid, const char* name)
{
	const bool reset = name == nullptr || *name == 0;
	const int tnum = reset ? INVALID_TRANSLATION : R_FindCustomTranslation(FName(name));
	// [Doruk-Aksoy] On a client only a clientside script runs, and its changes stay on its machine.
	const bool local = zx::net::InClientMode();
	if (tid == 0)
	{
		trnslate::SetActorTranslation(activator, reset, tnum, local);
		return;
	}
	FActorIterator it(tid);
	while (AActor* actor = it.Next())
		trnslate::SetActorTranslation(actor, reset, tnum, local);
}

int FriendlySeeBlocks(AActor* actor)
{
	return ComputeSeeBlocksRead(actor->fua_seeBlocks, actor->GetClass()->Meta.GetMetaInt(AMETA_FriendlySeeBlocks, 10));
}

void SetFriendlySeeBlocks(AActor* actor, int32_t blocks)
{
	actor->fua_seeBlocks = ComputeSeeBlocksStore(blocks);
}

int WaterDepth(AActor* actor)
{
	const sector_t* sec = actor->Sector;
	if (sec == nullptr)
		return 0;
	WaterDepthIn in = {};
	in.z = actor->z.Raw();
	in.height = actor->height.Raw();
	in.underwater = (sec->MoreFlags & SECF_UNDERWATER) != 0;
	const sector_t* hsec = sec->GetHeightSec();
	if (hsec != nullptr)
	{
		in.heightSec = true;
		in.heightSecWater = (hsec->MoreFlags & SECF_UNDERWATERMASK) != 0;
		in.fakeFloorOnly = (hsec->MoreFlags & SECF_FAKEFLOORONLY) != 0;
		in.heightSecFloor = hsec->floorplane.ZatPoint(actor->x, actor->y).Raw();
		in.heightSecCeiling = hsec->ceilingplane.ZatPoint(actor->x, actor->y).Raw();
	}
	else
	{
		// [Doruk-Aksoy] The first swimmable, non-solid 3D floor above the actor's feet whose bottom is at or below its middle, as UpdateWaterLevel finds it.
		for (unsigned i = 0; i < sec->e->XFloor.ffloors.Size(); i++)
		{
			const F3DFloor* rover = sec->e->XFloor.ffloors[i];
			if (!(rover->flags & FF_EXISTS) || !(rover->flags & FF_SWIMMABLE) || (rover->flags & FF_SOLID))
				continue;
			const fixed_t top = rover->top.plane->ZatPoint(actor->x, actor->y);
			if (top <= actor->z || rover->bottom.plane->ZatPoint(actor->x, actor->y) > actor->z + (actor->height >> 1))
				continue;
			in.swimFloor = true;
			in.swimTop = top.Raw();
			break;
		}
	}
	return (int)fixed_t::FromRaw(ComputeWaterDepth(in));
}

}} // namespace zx::acsparity
