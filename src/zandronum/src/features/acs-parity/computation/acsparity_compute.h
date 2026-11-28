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
	kSetSectorDamage = 94,
	kSetSectorTerrain = 95,
	kCheckProximity = 98,
	kCheckActorState = 99,
	kCheckClass = 200,
	kDamageActor = 201,
	kSetActorFlag = 202,
	kSetTranslation = 203,
	kGetActorFloorTerrain = 205,
	kStrArg = 206,
	kFloor = 207,
	kRound = 208,
	kCeil = 209,
	kGetNetID = 215,
	kSetActivatorByNetID = 216,
	kGetLineX = 300,
	kGetLineY = 301,
	kSetTeamScore = 19621,
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

// [Doruk-Aksoy] A damage type's old means-of-death number, the only form our sectors keep it in: 0 for none, -1 for a name they can't hold.
int ComputeDamageTypeMod(const char* name);

// [Doruk-Aksoy] The damage type a means-of-death number names, null for none.
const char* ComputeModDamageType(int mod);

// [Doruk-Aksoy] A sector damage amount held to the short our sectors keep it in; a negative one, which UZDoom heals with, is none.
int16_t ComputeSectorDamageAmount(int32_t amount);

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

// [Doruk-Aksoy] Whether DamageActor may hurt its target here: anywhere but a client, and there only an actor of the client's own.
bool ComputeClientMayDamage(bool inClientMode, bool clientSideOnly);

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
// [Doruk-Aksoy] Counting, as DECORATE's CountProximity, it scans every candidate and answers how many counted.
template <class Handle, class Next, class Sight>
ProximityScan<Handle> ComputeProximityScan(Next next, Sight sight, const ProximityPoint& ref, double distance, int count, int flags,
	bool counting = false)
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
		if (++counter > count && !counting)
		{
			stopped = true;
			if (!(setsPointer && prefers))
				break;
		}
	}
	scan.result = counting ? counter : ComputeProximityResult(counter, count, flags, stopped);
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

// [Doruk-Aksoy] UZDoom's actor property numbers for the values of its own these answer.
enum ActorProperty
{
	kAPropSoundClass = 47,
	kAPropFriendlySeeBlocks = 48,
	kAPropWaterDepth = 49,
};

// [Doruk-Aksoy] APROP_FriendlySeeBlocks: how far, in blockmap blocks, a friendly monster looks for enemies; 128 at most per search, so a script can't make every look scan the map.
const int32_t kMaxFriendlySeeBlocks = 128;

// [Doruk-Aksoy] An actor's own value as it keeps it, 0 meaning none so its class's stands: the value with its top bit flipped, so any value but INT32_MIN reads back.
int32_t ComputeSeeBlocksStore(int32_t value);

// [Doruk-Aksoy] What GetActorProperty reads: the actor's own value, else its class's.
int32_t ComputeSeeBlocksRead(int32_t stored, int32_t classDefault);

// [Doruk-Aksoy] The blocks a search of `blocks` takes, none for a negative value and kMaxFriendlySeeBlocks at most.
int32_t ComputeSeeBlocksSearch(int32_t blocks);

// [Doruk-Aksoy] What UZDoom's UpdateWaterDepth sees of an actor's sector, in fixed point, without the splash checks: an underwater sector, a Boom
// heightsec counted only as real water (UNDERWATERMASK), or the first swimmable, non-solid 3D floor whose top is above the actor and bottom at or below its middle.
struct WaterDepthIn
{
	bool underwater;		// [Doruk-Aksoy] SECF_UNDERWATER: the whole sector is water.
	bool heightSec;		// [Doruk-Aksoy] It takes its look from a Boom heightsec...
	bool heightSecWater;	// [Doruk-Aksoy] ...that is real water...
	bool fakeFloorOnly;	// [Doruk-Aksoy] ...and that only fakes the floor.
	int64_t heightSecFloor, heightSecCeiling;	// [Doruk-Aksoy] Its planes at the actor.
	bool swimFloor;		// [Doruk-Aksoy] A 3D floor of water holds the actor, with its top here.
	int64_t swimTop;
	int64_t z, height;
};

// [Doruk-Aksoy] APROP_WaterDepth: how deep in water the actor stands, never below 0.
int64_t ComputeWaterDepth(const WaterDepthIn &in);

// [Doruk-Aksoy] Which team score GetTeamScore reads and SetTeamScore writes in a game mode: frags where players earn frags, else wins where they earn wins, else points.
enum TeamScoreKind
{
	kTeamScoreFrags,
	kTeamScoreWins,
	kTeamScorePoints,
};

TeamScoreKind ComputeTeamScoreKind(bool earnsFrags, bool earnsWins);

// [Doruk-Aksoy] A sector plane's terrain: the one a script or the map gave it, else (-1) its flat's.
int ComputeSectorTerrain(int own, int flat);

// [Doruk-Aksoy] Whether SetSectorTerrain's plane names one, the floor (0) or the ceiling (1), as UZDoom's does; any other changes nothing.
bool ComputeTerrainPlane(int plane);

// [Doruk-Aksoy] The terrain an actor stands on: its sector's (a script's or the flat's) when it stands on that sector's own floor, else its floor flat's.
int ComputeThingFloorTerrain(bool onSectorFloor, int sectorTerrain, int floorPicTerrain);

}} // namespace zx::acsparity

#endif
