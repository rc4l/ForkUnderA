// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSPARITY_H
#define ZX_ACSPARITY_H

#include <cstdint>

#include "features/acs-parity/computation/acsparity_compute.h"

class AActor;
struct PClass;

namespace zx { namespace acsparity {

// [Doruk-Aksoy] CheckInventory, or GetMaxInventory when `max`: how much of `type` the actor carries, or the most it can.
int InventoryAmount(AActor* actor, const char* type, bool max);

// [Doruk-Aksoy] CheckProximity: whether `count` actors of `className` stand within `distance` (16.16) of the actor `ptr` selects from `self`, by CPXF_ `flags`.
int CheckProximity(AActor* self, const char* className, int32_t distance, int count, int flags, int ptr);

// [Doruk-Aksoy] The same scan for DECORATE's A_CheckProximity and CountProximity, distance in map units, counting answering how many counted.
int Proximity(AActor* self, const PClass* cls, double distance, int count, int flags, int ptr, bool counting);

// [Doruk-Aksoy] GetLineX (axis 0) or GetLineY (axis 1) of the first line with `lineId`, as 16.16, or 0 when no line has it.
int32_t LineCoord(int lineId, int axis, int32_t along, int32_t offset);

// [Doruk-Aksoy] The `index`th actor with `tid`, or `defactor` for tid 0.
AActor* NthActorFromTID(int tid, int index, AActor* defactor);

// [Doruk-Aksoy] The actor with net id `id`, or null.
AActor* ActorFromNetID(int32_t id);

// [Doruk-Aksoy] The name of the terrain under `actor`'s feet.
const char* FloorTerrainName(AActor* actor);

// [Doruk-Aksoy] SetSectorDamage: `amount` of damage of `type` to players in sectors with `tag`, as Sector_SetDamage gives it; a type sectors can't hold is normal damage, warned once, and a negative amount none.
void SetSectorDamage(int tag, int32_t amount, const char* type);

// [Doruk-Aksoy] Whether `name` is an actor class.
bool IsActorClass(const char* name);

// [Doruk-Aksoy] SetActorFlag: the flag set or cleared on the actors with `tid`, the activator for 0 (which alone reports an unknown flag), and how many have it.
int SetActorFlag(AActor* activator, int tid, const char* name, bool on);

// [Doruk-Aksoy] SetTranslation: the named translation on the actors with `tid`, the activator for 0, "" giving back each one's class's and an unknown name changing nothing.
void SetTranslation(AActor* activator, int tid, const char* name);

// [Doruk-Aksoy] APROP_FriendlySeeBlocks: the actor's own value, set by a script, else its class's (10 by default).
int FriendlySeeBlocks(AActor* actor);
void SetFriendlySeeBlocks(AActor* actor, int32_t blocks);

// [Doruk-Aksoy] APROP_SoundClass: the sound class a player's sounds come from, a script's over its skin's and class's, "player" for an actor no player holds.
const char* SoundClassOf(AActor* actor);
// [Doruk-Aksoy] A script's sound class for the player whose pawn `actor` is, "" giving it back; a server tells every client, a clientside script changes its own copy.
void SetSoundClass(AActor* actor, const char* name);

// [Doruk-Aksoy] APROP_WaterDepth: how deep in water the actor stands, as GetActorProperty gives fixed values, worked out when asked.
int WaterDepth(AActor* actor);

}} // namespace zx::acsparity

#endif
