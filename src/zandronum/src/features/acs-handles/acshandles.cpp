// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/acs-handles/acshandles.h"

#include <cstddef>
#include <string>
#include <vector>

#include "actor.h"
#include "d_player.h"
#include "dthinker.h"
#include "farchive.h"
#include "g_level.h"
#include "g_shared/a_pickups.h"
#include "m_png.h"
#include "name.h"
#include "c_console.h"
#include "c_dispatch.h"

// [Doruk-Aksoy] The SAVEVER from which actors save their handle slot.
static const int kHandleSaveVersion = 4533;
extern int SaveVersion;

namespace zx { namespace acshandles {

namespace {

// [Doruk-Aksoy] This machine's table: a server's for its scripts, a client's for its CLIENTSIDE ones.
HandleTable g_table;

const DWORD kChunkId = MAKE_ID('f', 'h', 'N', 'd');

// [Doruk-Aksoy] Whether this map already warned of an actor out of keys.
bool g_warnedFull = false;

// [Doruk-Aksoy] A key as a name number, 0 for none; looking up never adds a name.
int KeyOf(const char *key, bool create)
{
	if (key == NULL || *key == 0)
		return 0;
	return FName(key, !create).GetIndex();
}

int CurrentMap()
{
	return FName(level.MapName.GetChars()).GetIndex();
}

// [Doruk-Aksoy] A player's pawn and what it carries travel to the next map rather than staying with the hub map.
bool Travels(AActor *actor)
{
	if (actor->player != NULL && actor->player->mo == actor)
		return true;
	if (actor->IsKindOf(RUNTIME_CLASS(AInventory)))
	{
		AActor *owner = static_cast<AInventory *>(actor)->Owner;
		return owner != NULL && owner->player != NULL && owner->player->mo == owner;
	}
	return false;
}

// [Doruk-Aksoy] Every actor forgets its slot, so destroying it releases nothing of a table that is about to be replaced.
void ForgetSlots()
{
	TThinkerIterator<AActor> it;
	for (AActor *actor = it.Next(); actor != NULL; actor = it.Next())
		actor->fua_handleSlot = 0;
}

} // namespace

int32_t HandleFor(AActor *actor)
{
	if (actor == NULL)
		return 0;
	if (actor->fua_handleSlot == 0)
	{
		int released = 0;
		const uint32_t slot = g_table.Acquire(actor, released);
		if (released != 0)
			Printf(TEXTCOLOR_ORANGE "ACS handles: the table is full, so hub map %s gave back its handles.\n", FName(ENamedName(released)).GetChars());
		if (slot == 0)
		{
			Printf(TEXTCOLOR_RED "ACS handles: no handle is left for %s.\n", actor->GetClass()->TypeName.GetChars());
			return 0;
		}
		actor->fua_handleSlot = slot;
	}
	return g_table.HandleOf(actor->fua_handleSlot);
}

AActor *ActorOf(int32_t handle)
{
	return const_cast<AActor *>(static_cast<const AActor *>(g_table.Resolve(handle)));
}

void ReleaseActor(AActor *actor)
{
	if (actor->fua_handleSlot == 0)
		return;
	g_table.Release(actor->fua_handleSlot);
	actor->fua_handleSlot = 0;
}

void SerializeActor(FArchive &arc, AActor *actor)
{
	if (SaveVersion < kHandleSaveVersion)
	{
		if (arc.IsLoading())
			actor->fua_handleSlot = 0;
		return;
	}
	DWORD slot = actor->fua_handleSlot;
	arc << slot;
	if (arc.IsLoading())
		actor->fua_handleSlot = (slot != 0 && g_table.Attach(slot, actor)) ? slot : 0;
}

void ParkLevel()
{
	const int map = CurrentMap();
	TThinkerIterator<AActor> it;
	for (AActor *actor = it.Next(); actor != NULL; actor = it.Next())
		if (actor->fua_handleSlot != 0 && !Travels(actor))
			g_table.Park(actor->fua_handleSlot, map);
}

void EnterLevel()
{
	g_table.Visit(CurrentMap());
	g_warnedFull = false;
}

void ReleaseParked()
{
	g_table.ReleaseAllParked();
}

void ResetTable()
{
	ForgetSlots();
	g_table.Reset();
}

void WriteTable(FILE *file)
{
	TableState state = g_table.Save();
	std::vector<int> keys;
	std::vector<std::string> names;
	// [Doruk-Aksoy] Map and data keys alike are name numbers, saved as their names.
	const auto toIndex = [&](int name) {
		for (size_t i = 0; i < keys.size(); ++i)
			if (keys[i] == name)
				return int(i + 1);
		keys.push_back(name);
		names.push_back(FName(ENamedName(name)).GetChars());
		return int(keys.size());
	};
	ComputeRemapMaps(state, toIndex);
	ComputeRemapDataKeys(state, toIndex);
	const std::vector<uint8_t> chunk = ComputeEncodeTable(state, names);
	M_AppendPNGChunk(file, kChunkId, chunk.data(), DWORD(chunk.size()));
}

void ReadTable(PNGHandle *png)
{
	ForgetSlots();
	const unsigned int length = M_FindPNGChunk(png, kChunkId);
	if (length == 0)
	{
		g_table.Reset();
		return;
	}
	std::vector<uint8_t> chunk(length);
	const size_t read = fread(chunk.data(), 1, length, png->File->GetFile());
	png->File->ResetFilePtr();
	TableState state;
	std::vector<std::string> names;
	if (read != length || !ComputeDecodeTable(chunk.data(), chunk.size(), state, names))
	{
		Printf(TEXTCOLOR_ORANGE "ACS handles: the savegame's handle table is damaged, so no saved handle resolves.\n");
		g_table.Reset();
		return;
	}
	const auto toName = [&](int index) {
		return index >= 1 && size_t(index) <= names.size() ? FName(names[index - 1].c_str()).GetIndex() : 0;
	};
	ComputeRemapMaps(state, toName);
	ComputeRemapDataKeys(state, toName);
	g_table.Load(state);
}

void FinishLoad()
{
	g_table.ReleaseUnattached();
}

bool SetActorData(AActor *actor, const char *key, int32_t value)
{
	const int name = KeyOf(key, true);
	if (actor == NULL || name == 0 || HandleFor(actor) == 0)
		return false;
	const DataResult result = g_table.SetData(actor->fua_handleSlot, name, value);
	if (result == kDataFull && !g_warnedFull)
	{
		Printf(TEXTCOLOR_ORANGE "fua_SetActorData: a %s already carries %u keys, so \"%s\" was not set.\n", actor->GetClass()->TypeName.GetChars(), unsigned(kMaxDataKeys), key);
		g_warnedFull = true;
	}
	return result == kDataSet;
}

int32_t GetActorData(AActor *actor, const char *key, int32_t fallback)
{
	int32_t value = fallback;
	const int name = KeyOf(key, false);
	if (actor != NULL && name != 0 && actor->fua_handleSlot != 0)
		g_table.GetData(actor->fua_handleSlot, name, value);
	return value;
}

bool HasActorData(AActor *actor, const char *key)
{
	int32_t value;
	const int name = KeyOf(key, false);
	return actor != NULL && name != 0 && actor->fua_handleSlot != 0 && g_table.GetData(actor->fua_handleSlot, name, value);
}

void ClearActorData(AActor *actor, const char *key)
{
	if (actor == NULL || actor->fua_handleSlot == 0)
		return;
	if (key == NULL)
		g_table.ClearAllData(actor->fua_handleSlot);
	else if (const int name = KeyOf(key, false))
		g_table.ClearData(actor->fua_handleSlot, name);
}

}} // namespace zx::acshandles

// [Doruk-Aksoy] How this machine's table stands, for tests: its slots by state.
DEV_MODE_CCMD(fua_handles)
{
	using namespace zx::acshandles;
	Printf("fua_handles: live %u parked %u free %u retired %u\n", unsigned(g_table.CountState(kSlotLive)), unsigned(g_table.CountState(kSlotParked)),
		unsigned(g_table.CountState(kSlotFree)), unsigned(g_table.CountState(kSlotRetired)));
}
