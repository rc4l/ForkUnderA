// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#ifndef ZX_ACSHANDLES_H
#define ZX_ACSHANDLES_H

#include <cstdint>
#include <cstdio>

#include "features/acs-handles/computation/handles_compute.h"

class AActor;
class FArchive;
struct PNGHandle;

namespace zx { namespace acshandles {

// [Doruk-Aksoy] The ACS function numbers, fua_ block 24020-24023.
enum Function
{
	kGetActorHandle = 24020,
	kHandleValid = 24021,
	kHandlePointer = 24022,
	kSetActivatorHandle = 24023,
	// [Doruk-Aksoy] The per-actor key/value store, 24032-24035.
	kSetActorData = 24032,
	kGetActorData = 24033,
	kHasActorData = 24034,
	kClearActorData = 24035,
};

// [Doruk-Aksoy] The actor's handle, given out the first time a script asks, or 0 for no actor or a full table.
int32_t HandleFor(AActor *actor);

// [Doruk-Aksoy] The actor a handle names, or null once it is gone.
AActor *ActorOf(int32_t handle);

// [Doruk-Aksoy] From AActor::Destroy: the actor's handles go stale, unless its hub map just parked it.
void ReleaseActor(AActor *actor);

// [Doruk-Aksoy] From AActor::Serialize: the actor's slot, and on loading its claim on that slot.
void SerializeActor(FArchive &arc, AActor *actor);

// [Doruk-Aksoy] Leaving a hub map for another of its hub: every actor of the level but the travelling players parks its slot.
void ParkLevel();

// [Doruk-Aksoy] A level was entered, which counts as a visit to its map for the release-oldest valve.
void EnterLevel();

// [Doruk-Aksoy] The hub's snapshots are gone, so its parked slots are released.
void ReleaseParked();

// [Doruk-Aksoy] World and global variables were cleared, so no variable holds a handle: the table starts over.
void ResetTable();

// [Doruk-Aksoy] The table's savegame chunk, read before the level is rebuilt so its actors can claim their slots.
void WriteTable(FILE *file);
void ReadTable(PNGHandle *png);

// [Doruk-Aksoy] After a savegame's level is rebuilt, the live slots no actor claimed are released.
void FinishLoad();

// [Doruk-Aksoy] fua_SetActorData: the actor's value under `key`, false past its 64 keys (warned once a map) or for no actor.
bool SetActorData(AActor *actor, const char *key, int32_t value);

// [Doruk-Aksoy] fua_GetActorData and fua_HasActorData: the value under `key`, `fallback` when unset.
int32_t GetActorData(AActor *actor, const char *key, int32_t fallback);
bool HasActorData(AActor *actor, const char *key);

// [Doruk-Aksoy] fua_ClearActorData: one key removed, or every key when `key` is null.
void ClearActorData(AActor *actor, const char *key);

}} // namespace zx::acshandles

#endif
