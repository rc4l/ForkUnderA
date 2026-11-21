//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: ghostactor.h
//
// Description: The client's mirror actors: the render shim that draws
// each snapshot ghost as a real AActor, its local animation and smoothing,
// cosmetic event copies, mirror spawns, and the client report.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_GHOSTACTOR_H
#define ZX_NETSNAPSHOT_GHOSTACTOR_H

class AActor;	// declared here so an includer needs no particular header order

// Each present ghost is drawn by a client-side actor of its class that never
// ticks, written from the sampler each frame (cl_ghostactor.md).

namespace zx { namespace netsnapshot {

// Called once per rendered frame, before the world is drawn.
void GhostFrame();

// [Kaori-Chani] The fold on the packet clock, called once for each payload applied: the
// state it leaves is what the frame's own folds are taken from.
void GhostFoldApplied(unsigned int nowMS);
// The [snap] report now, clearing its counts as the 5 s one does: fua_snap_fuzz
// calls it so its counts don't reach the next one.
void GhostReportNow();

// Hides the legacy actors for what the delta just applied brought, called from
// the packet path right after ApplyDelta, not the render loop.
void HideEnteredLegacy();

// The mirror actor for a NetID, or NULL: a real AActor of the right class
// at the ghost's drawn position, so it is a valid sound source for an
// entity the client has no legacy actor for. bCount false keeps a per-tic
// retry (a point pusher's source) out of the mirror lookup line.
AActor* MirrorForNetID(unsigned int id, bool bCount = true);
// For CLIENT_ReadActorFromNetID: an id with no legacy actor resolves to its mirror for the
// few commands that may use one, each resolution counted; a no-op for any other command.
void ResolveThroughMirror(unsigned short netID, const PClass *subclass, AActor *&actor,
	const char *commandName);

// The property sets this client is holding, and where they have gone.
unsigned int PropsKept();
void PropsStats(unsigned int & toMirror, unsigned int & held, bool reset);

// An item respawn for a copy behind a mirror: the record draws the item, so the copy stays
// hidden and only its sound and fog are made, at the ghost. True when it answered.
bool RespawnBehindMirror(AActor *pActor, bool bFog);

// A snapshot point sound for an entity with no mirror yet, moved onto the
// mirror when it appears, if the S_Sound just made started it (bStarted).
void PointSoundPending(unsigned int netID, fixed_t x, fixed_t y, fixed_t z,
	int channel, int soundId, bool bStarted);
// Counted by the caller when the mirror already existed on arrival.
void CountPointSoundOnMirror();

}} // namespace zx::netsnapshot

class DThinker;
struct BYTESTREAM_s;

namespace zx { namespace netsnapshot {

// Cosmetic events (design doc 6.3, fua_snap_cosmetic_events 2).
// SVC2_SNAPCOSMETIC: make each event's copy.
void CosmeticEvents(BYTESTREAM_s *pByteStream);
// Clears the stats line's event counts, for fua_snap_fuzz to drop its own.
void CosmeticCountsReset();
// Spawn: a copy run forward by its age passes the rest on to what it spawns.
void CosmeticSpawn(AActor *pActor);
// TickThinkers, before the first Tick: run the copy forward by its age.
void CosmeticFirstTick(DThinker *pThinker);
// AActor::Destroy.
void CosmeticForget(AActor *pActor);

}} // namespace zx::netsnapshot

// Mirror spawns (design doc 6.3): while this is up,
// SetState runs a mirror's spawner actions, as this lets them.
struct FState;
extern bool g_bSnapMirrorSpawns;

namespace zx { namespace netsnapshot {

bool MirrorSpawnAction(const FState *pState);

// The player number a mirror stands in for, or -1, by the reserved id rule
// (3.1), since a mirror's NetID is 0.
int MirrorPlayerNum(const AActor* pActor);

// Destroys every mirror actor, in a live level only; a level going away
// calls LevelUnloaded instead.
void GhostClearAll();

// Map units a tic this class can cover, from its default Speed (0 unknown).
unsigned int SpeedForClass(unsigned int classId);

// Drops every mirror reference without dereferencing any, and the ghost table,
// as every level teardown must.
void LevelUnloaded();

// Enable/disable ghost mirroring because live snapshot traffic started or
// stopped, without the player toggling anything.
void GhostsFromNetwork(bool active);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_GHOSTACTOR_H
