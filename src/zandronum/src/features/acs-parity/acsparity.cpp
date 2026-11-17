// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-parity/acsparity.h"

#include "actor.h"
#include "actorptrselect.h"
#include "d_player.h"
#include "g_shared/a_pickups.h"
#include "p_local.h"
#include "p_tags.h"
#include "p_terrain.h"
#include "r_state.h"

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
	AActor* ref = COPY_AAPTR(self, ptr);
	const PClass* cls = className != nullptr ? PClass::FindClass(className) : nullptr;
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
	const ProximityScan<AActor*> scan = ComputeProximityScan<AActor*>(next, sight, PointOf(ref), distance / 65536.0, count, flags);
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

bool IsActorClass(const char* name)
{
	const PClass* cls = name != nullptr ? PClass::FindClass(name) : nullptr;
	return cls != nullptr && cls->IsDescendantOf(RUNTIME_CLASS(AActor));
}

}} // namespace zx::acsparity
