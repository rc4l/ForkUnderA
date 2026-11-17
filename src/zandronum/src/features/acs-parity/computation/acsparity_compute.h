// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSPARITY_COMPUTE_H
#define ZX_ACSPARITY_COMPUTE_H

#include <cmath>
#include <cstdint>

namespace zx { namespace acsparity {

// [Doruk-Aksoy] UZDoom's ACS function numbers, which compiled mods hold.
enum Function
{
	kGetMaxInventory = 93,
	kCheckProximity = 98,
	kCheckActorState = 99,
	kCheckClass = 200,
	kDamageActor = 201,
	kGetActorFloorTerrain = 205,
	kStrArg = 206,
	kFloor = 207,
	kRound = 208,
	kCeil = 209,
	kGetNetID = 215,
	kSetActivatorByNetID = 216,
	kGetLineX = 300,
	kGetLineY = 301,
};

// [Doruk-Aksoy] UZDoom's CheckProximity flags (CPXF_ in p_local.h), which zt-bcc's zcommon.bcs names too.
enum ProximityFlag
{
	kCpxAncestor = 1 << 0,
	kCpxLessOrEqual = 1 << 1,
	kCpxNoZ = 1 << 2,
	kCpxCountDead = 1 << 3,
	kCpxDeadOnly = 1 << 4,
	kCpxExact = 1 << 5,
	kCpxSetTarget = 1 << 6,
	kCpxSetMaster = 1 << 7,
	kCpxSetTracer = 1 << 8,
	kCpxFarthest = 1 << 9,
	kCpxClosest = 1 << 10,
	kCpxSetOnPtr = 1 << 11,
	kCpxCheckSight = 1 << 12,
};

// [Doruk-Aksoy] A 16.16 value rounded down to a whole number.
int32_t ComputeFixedFloor(int32_t x);

// [Doruk-Aksoy] A 16.16 value rounded to the nearest whole number, halves up.
int32_t ComputeFixedRound(int32_t x);

// [Doruk-Aksoy] A 16.16 value rounded up to a whole number, where UZDoom adds one to a whole number.
int32_t ComputeFixedCeil(int32_t x);

// [Doruk-Aksoy] UZDoom's DoubleToACS: map units to 16.16, rounded half to even (xs_CRoundToInt), held to the int range.
int32_t ComputeFixedFromDouble(double units);

// [Doruk-Aksoy] GetLineX (axis 0) or GetLineY (axis 1): the point `along` the line from its first vertex, moved `offset` units along its right-hand normal.
double ComputeLineCoord(int axis, double v1x, double v1y, double dx, double dy, double along, double offset);

// [Doruk-Aksoy] Whether `id` can name an actor's net id, which is an unsigned short above 0.
bool ComputeNetIdValid(int32_t id);

// [Doruk-Aksoy] The `index`th thing `next` hands out, or a null one past the last or for a negative index.
template <class Next>
auto ComputeNth(Next next, int index) -> decltype(next())
{
	if (index < 0)
		return nullptr;
	for (auto item = next(); item != nullptr; item = next())
		if (index-- == 0)
			return item;
	return nullptr;
}

// [Doruk-Aksoy] Where CheckProximity's reference and each candidate stand, in map units.
struct ProximityPoint
{
	double x, y, z, top;
};

// [Doruk-Aksoy] UZDoom's range test: closer than `distance` across the ground and, unless CPXF_NOZ, between the two bodies vertically.
bool ComputeProximityInRange(const ProximityPoint& ref, const ProximityPoint& mo, double distance, int flags);

// [Doruk-Aksoy] Whether a candidate that is dead, or alive, counts under `flags`.
bool ComputeProximityLifeCounts(bool killed, int flags);

// [Doruk-Aksoy] CheckProximity's answer once `counter` candidates counted, the scan having stopped early or not.
int ComputeProximityResult(int counter, int count, int flags, bool stoppedEarly);

template <class Handle>
struct ProximityScan
{
	int result;
	Handle chosen;	// [Doruk-Aksoy] The candidate a CPXF_SET flag points to, or a null one.
};

// [Doruk-Aksoy] UZDoom's P_Thing_CheckProximity loop: `next(handle, point, killed)` hands out each candidate of the class until it returns false, `sight(handle)` is asked only under CPXF_CHECKSIGHT.
template <class Handle, class Next, class Sight>
ProximityScan<Handle> ComputeProximityScan(Next next, Sight sight, const ProximityPoint& ref, double distance, int count, int flags)
{
	ProximityScan<Handle> scan = { 0, nullptr };
	if (distance <= 0)
		return scan;
	const bool setsPointer = (flags & (kCpxSetTarget | kCpxSetMaster | kCpxSetTracer)) != 0;
	const bool prefers = (flags & (kCpxClosest | kCpxFarthest)) != 0;
	double closer = distance, farther = 0;
	int counter = 0;
	bool stopped = false;
	Handle handle = nullptr;
	ProximityPoint point = {};
	bool killed = false;
	while (next(handle, point, killed))
	{
		if (!ComputeProximityInRange(ref, point, distance, flags))
			continue;
		if ((flags & kCpxCheckSight) && !sight(handle))
			continue;
		if (!ComputeProximityLifeCounts(killed, flags))
			continue;
		if (setsPointer)
		{
			const double current = std::hypot(point.x - ref.x, point.y - ref.y);
			if ((flags & kCpxClosest) && current < closer)
			{
				scan.chosen = handle;
				closer = current;
			}
			else if ((flags & kCpxFarthest) && current > farther)
			{
				scan.chosen = handle;
				farther = current;
			}
			else if (scan.chosen == nullptr)
				scan.chosen = handle;
		}
		if (++counter > count)
		{
			stopped = true;
			if (!(setsPointer && prefers))
				break;
		}
	}
	scan.result = ComputeProximityResult(counter, count, flags, stopped);
	return scan;
}

// [Doruk-Aksoy] What CheckInventory reads of a name: health, armor (BasicArmor) or an item class.
enum InventoryKind
{
	kInventoryItem,
	kInventoryHealth,
	kInventoryArmor,
};

InventoryKind ComputeInventoryKind(const char* type);

// [Doruk-Aksoy] What CheckInventory, or GetMaxInventory when `max`, answers for an item class.
struct InventoryState
{
	bool held;			// [Doruk-Aksoy] The actor carries one.
	int amount;			// [Doruk-Aksoy] The carried one's Amount and MaxAmount.
	int maxAmount;
	bool isInventory;	// [Doruk-Aksoy] The class is an inventory item, with its default MaxAmount.
	int defaultMax;
};

int ComputeInventoryAmount(const InventoryState& state, bool max);

}} // namespace zx::acsparity

#endif
