//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: capture.h
//
// Description: The server's snapshot tick: the world capture, legacy
// suppression, mover capture, cosmetic events, snapshot sounds and the
// stats report. The per-client delta lives in clienttrack.h.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_CAPTURE_H
#define ZX_NETSNAPSHOT_CAPTURE_H

// SnapPSpriteState and the other wire types, included here, not left to order.
#include "features/net-snapshot/format.h"

// The pump, at the end of every SERVER_Tick: the capture, each client's delta
// sent as SVC2_SNAPSHOT datagrams, and the statistics (sv_snapshot.md).

namespace zx { namespace netsnapshot {

void ServerTick();

}} // namespace zx::netsnapshot

// The legacy suppression levels, each a superset of the one below, and the
// commands each stops (sv_snapshot.md).
enum
{
	SNAPLEGACY_MOTION		= 1,
	SNAPLEGACY_ANIM			= 2,
	SNAPLEGACY_EXISTENCE	= 3,
};

class AActor;

namespace zx { namespace netsnapshot {

// True when the snapshot channel has taken over this update class for this
// actor, so no legacy command is sent (players: SuppressesLegacyPlayer).
bool SuppressesLegacy( const AActor *pActor, int updateClass );

// Called by SERVER_SendClientPacket once an unreliable datagram is launched,
// with its payload size and its size on the wire, which only that site sees.
void NoteUnreliablePacket(unsigned int payloadBytes, unsigned int wireBytes, bool usedZstd);

// Called when a snapshot command is queued for a client, so the wire accounting
// can tell which datagrams carried one.
void NoteSnapshotQueued(unsigned int client);
bool ConsumeSnapshotQueued(unsigned int client);

// A client's ack of a snapshot datagram's sequence (datagrams, not frames).
void ClientAcknowledged(unsigned int client, unsigned int seq, unsigned int history);

// Per-client lifecycle, both of which must be wired, or a recycled slot is
// delta'd against the previous player's world.
void ClientConnected(unsigned int client);
void ClientDisconnected(unsigned int client);

// Called just before a level loads, forgetting presence on both sides, since
// IDList::clear hands the old map's ids to the new map's actors.
void MapChanged();

}} // namespace zx::netsnapshot

struct SnapFrame;

namespace zx { namespace netsnapshot {

// The loaded map's position frame, which SnapJoin carries and every position
// written for the map is relative to, computed on first use after a map change.
const SnapFrame& ServerMapFrame();

// The end of a map reset: counts it and sends DestroyAllSectorMovers with the
// count, or leaves that to the pump under fua_snap_debug_resetsplit.
void ResetEnd();
// For DestroyAllSectorMovers and SnapJoin.
BYTE MapStamp();
BYTE MapResetCount();
BYTE MapResetCountSent();
// A CLC_MISSINGPACKET entry for a sequence not yet sent, which is skipped.
void CountUnsentRequest();

// Legacy traffic by command (SVC, or SVC2 when extended), raw bytes summed over
// recipients, from the stock send paths for Phase 6's reclassify order.
void CountLegacyCommand(int svc, int svc2, unsigned int bytes, bool bReliable);
// A reliable packet's body on its first send and on a resend.
void CountLegacyFlush(unsigned int bytes);
void CountLegacyResend(unsigned int bytes);
// Time in SERVER_SendOutPackets and the resend tick, raw counter units.
void CountLegacySendRaw(QWORD raw);

// From S_Sound: an actor's sound to the clients that could hear it, for an
// entity whose legacy replication is suppressed, false when the caller should
// fall back to the legacy broadcast (bSoundOnClient: whether stock tells any).
bool BroadcastActorSound(AActor *pActor, int channel, int soundId,
	float volume, float attenuation, bool bSoundOnClient = true);

// True, and counted, for a sound no client can play (no sound, or no volume).
bool SoundUnplayable(int soundId, float volume);

// Called as Spawn hands out a NetID, counting those a cosmetic actor's took.
void NoteSpawnNetID(AActor *pActor);

}} // namespace zx::netsnapshot

// Cosmetic events (fua_snap_cosmetic_events 1 reports, 2 acts).
class DThinker;
extern DThinker *g_pSnapTickSelf;            // the thinker ticking, for Spawn's spawner

namespace zx { namespace netsnapshot {

// Every spawn, before its NetID; true when it must get none (mode 2).
bool NoteCosmeticSpawn(AActor *pActor);
void NoteCosmeticNetID(AActor *pActor);  // after the NetID, for report mode
void NoteFirstTick(DThinker *pThinker);  // after PostBeginPlay, before the first Tick
void NoteFirstTickDone(DThinker *pThinker);  // after the first Tick
// Server scripts that hold a copy: one started with the actor as activator, the
// end of a RunScript, and SetActorState or SetActorProperty on it.
void NoteScriptStart(AActor *pActivator, bool bClientSide);
void NoteScriptStopped(AActor *pActivator);
void NoteScriptChange(AActor *pActor);
void ForgetCosmetic(AActor *pActor);     // AActor::Destroy
// A map thing the server spawned, counted for layer 1 (P_SpawnThings).
void NoteMapThing(AActor *pActor);
// DLevelScript::DoSpawn, whose Spawn has no spawner, marking the actor after.
void AcsSpawnScope(bool bOn);
void NoteAcsSpawn(AActor *pActor);
void NoteBloodSpawn(AActor *pBlood);     // P_SpawnBlood, P_BloodSplatter(2)
void NoteRipperBlood(AActor *pBlood);    // P_RipperBlood
// Where P_SpawnBlood (which 0) or a splatter (1, 2) tells the clients, true
// when the event replaces stock's command (mode 2).
bool NoteBloodEvent(fixed_t x, fixed_t y, fixed_t z, unsigned int dir, int damage,
	AActor *pOriginator, int which);
void NotePuff(AActor *pPuff, AActor *pSource, bool bTellClients);
// One entity record the snapshot emitted: enter, update or leave.
void NoteCosmeticRecord(unsigned int id, unsigned int bytes);
// Mode 2: the actor reaches clients as an event, never a legacy command.
bool CosmeticRouted(const AActor *pActor);
// Mode 2: a sound the actor's own action plays, which its client copy plays.
bool CosmeticMutesSound(const AActor *pActor);

}} // namespace zx::netsnapshot

// Client: while an actor is run forward by its age, a repeat of its CHAN_AUTO
// sound does not start again.
extern bool g_bSnapCatchUp;

namespace zx { namespace netsnapshot {

bool CatchUpHoldsSound(AActor *pActor, int soundId);

// Both sides' hash of the first `count` names of S_sfx, in unsigned int, since
// windows.h makes DWORD a different symbol in some units.
void SoundTableHash(unsigned int count, unsigned int& lo, unsigned int& hi);
// After S_Start's SNDINFO rebuild: this map's table size and hash.
void TakeSoundTable();
// After SnapJoin: that size and hash, to one client.
void SendSoundTable(unsigned int client);
// The client's answer from CLC_SNAPSHOTACK (SNAP_SOUNDIDS_* bits).
void ClientSoundIds(unsigned int client, BYTE answer);

// Whether this actor left its animation chain this tic, which the per-client
// pass sends as a SNAPEV_BRANCH event.
bool BranchedThisTic(unsigned int id);

}} // namespace zx::netsnapshot

// Declared only, for units without info.h or dobjtype.h; PClass is a struct, or
// MSVC warns C4099 where the real one is seen.
struct FState;
struct PClass;
struct sector_t;

namespace zx { namespace netsnapshot {

// The gravity class's acceleration in fixed units per tic per tic: GetGravity
// from the class defaults, since a mirror forces MF_NOGRAVITY on itself.
double LawGravity(const PClass *pType, const sector_t *pSector);
// The same at a record's position, with the compat order, which both ends call
// on the record they extrapolate from; false for an unknown class.
bool LawAt(unsigned int classId, fixed_t x, fixed_t y, double &g, bool &bOldZ);

// Every snapshot cvar not at its default: the server at its first snapshot
// tick, a client when snapshot traffic starts.
void ReportCvars();

// Mirror spawns: the four spawner actions, and a class owning a parameter
// expression that runs a special.
bool IsSpawnerState(const FState *pState);
bool OwnsSpecialExpression(const PClass *pType);
// A spawner state on the class or an ancestor, cached by class index.
bool ClassSpawns(const PClass *pType);
// Would this state's A_CustomMissile spawn on pSelf if it had a target:
// 0 no (not A_CustomMissile, refused by the client gate, or aim mode 2),
// 1 yes, 2 unknown (a computed class or flags parameter).
int CustomMissileNeedsTarget(const AActor *pSelf, const FState *pState);
// The shooter NetID the capture holds for a spawner missile, 0 for none.
WORD EnterTarget(unsigned int id);

}} // namespace zx::netsnapshot

// Walks a state chain forward by exactly `elapsedTics` and returns where it
// lands, or NULL at a dead end; `ticsRemaining` is the starting phase
// (negative: a state the walk cannot leave), and `pFlags` may be NULL.
#define SNAP_WALK_RNG		1	// crossed a state whose duration is random
#define SNAP_WALK_SCALED	2	// crossed a Fast/Slow state, scaled per actor
#define SNAP_WALK_HELD		4	// stopped on a state it can never leave
#define SNAP_WALK_CAPPED	8	// hit the step bound; the result is not a landing

namespace zx { namespace netsnapshot {

FState * WalkChain(FState * pFrom, int ticsRemaining, int elapsedTics,
	int * pFlags);

// Resolves a wire qstate against a class, the one resolver for both ends.
FState * StateFromQState(const PClass * pType, WORD qstate);

// The qstate for a state, the one encoder for the capture and the test below.
WORD QStateForState(const PClass * pType, FState * pState);

}} // namespace zx::netsnapshot

// Appearance suppression: whether the walk from what the client was told
// reaches what the server holds now, from wire values only.
#define SNAP_APPEAR_DERIVABLE	0	// client gets here unaided; withhold
#define SNAP_APPEAR_DIVERGED	1	// walk landed elsewhere; send
#define SNAP_APPEAR_UNWALKABLE	2	// no route the client can reproduce; send

// A bound on the walk's work and its random-duration states, not a client cap.
#define SNAP_APPEAR_MAX_TICS	70

namespace zx { namespace netsnapshot {

int AppearDerivable(WORD classId, WORD wasQState, BYTE wasQTics,
	int elapsedTics, WORD curQState);

// This tic's psprite state for every player slot (MAXPLAYERS, by player
// number), weaponClass 0 for a slot with no player, pawn or weapon.
const SnapPSpriteState * PSpriteCapture();

// The reserved entity id for a player number, or 0, by IDList's rule (3.1).
WORD PlayerReservedId(unsigned int playernum);

}} // namespace zx::netsnapshot

// Why a local block was or was not captured, a code per refusal.
enum
{
	SNAPLOCAL_CAP_OK = 0,
	SNAPLOCAL_CAP_NOPAWN,		// not in game, or no pawn yet
	SNAPLOCAL_CAP_SPECTATOR,	// excluded on purpose; position is client-owned
	SNAPLOCAL_CAP_NOCLIENT,		// no CLIENT_s: should never read nonzero
};

namespace zx { namespace netsnapshot {

// True only on the first enter emitted to `player` for this missile, giving its
// spawn tic, from a ring of recently fired missiles.
bool OwnMissileFirstEnter(WORD id, unsigned int player, int & spawnTic);

// Player-fired missiles seen spawning, ring entries overwritten before an enter
// went out (more than a trickle: the ring is too small), and re-sends refused.
void OwnMissileStats(unsigned int & spawns, unsigned int & evicted,
	unsigned int & resent, bool reset);

// The local player's block for one client, `out` zeroed on any refusal.
int LocalCapture(unsigned int playernum, SnapLocalState & out);

// Whether this client's periodic MovePlayer for OTHER players is suppressed
// (level 3).
bool SuppressesLegacyPlayer(unsigned int client);

// Client-frames the gate above suppressed since the last reset.
QWORD LegacyPlayerSkips(bool bReset);

// Mover capture, behind fua_snap_movers: a thinker hands its state in, `pOwner`
// naming the slot and `bGone` marking it destroyed.
void MoverCapturePlat(const void * pOwner, WORD index,
	const SnapMoverPlat & s, bool bBlocked, bool bGone);
void MoverCaptureCeiling(const void * pOwner, WORD index,
	const SnapMoverCeiling & s, bool bBlocked, bool bGone);
void MoverCaptureFloor(const void * pOwner, WORD index,
	const SnapMoverFloor & s, bool bStair, bool bBlocked, bool bGone);
void MoverCaptureDoor(const void * pOwner, WORD index,
	const SnapMoverDoor & s, bool bBlocked, bool bGone);
void MoverCaptureElevator(const void * pOwner, WORD index,
	const SnapMoverElevator & s, bool bBlocked, bool bGone);
void MoverCapturePillar(const void * pOwner, WORD index,
	const SnapMoverPillar & s, bool bGone);
// A whole polyobject by its FPolyObj and polyobjs[] index, no entries the GONE;
// `overCap` counts entries the full body left out, and `bAllocate` false only
// updates a live slot.
void MoverCapturePoly(const void * pOwner, WORD index,
	const SnapMoverPoly & s, unsigned int overCap, bool bAllocate);
// Polyobject slots reclaimed, entries over cap, and rotator re-anchors.
void MoverPolyStats(unsigned int & reclaimed, unsigned int & overCap,
	unsigned int & resyncs, bool bReset);
// A fresh action generation, from the counter every mover slot draws on.
BYTE NewMoverGen();
// The capture's own gate.
bool MoversOn();
// Whether the snapshot is on as the server is acting on it: fua_snap latched
// at a map load, since an on mid-map waits for the next one.
bool GateOn();

// The legacy mover gate, latched once a pump, which stops the tracked classes'
// state commands and the door sound.
bool MoverGate();

// Every door sound passes here, sent to clients while that gate is on: a gated client runs
// the door off records and plays nothing itself.
void MoverSendDoorSound( sector_t * sec, int choice, bool bCheckSequence );

// Every ceiling sound passes here, and exactly one command goes out: keyed on the sector while
// the gate is on, since the client's ceiling waits on a record, and on the ceiling otherwise.
void MoverSendCeilingSound( sector_t * sec, int ceilingID, int silent );

// Every plat sound passes here, and exactly one command goes out: keyed on the sector while
// the gate is on, since the client's plat waits on a record, and on the plat otherwise.
void MoverSendPlatSound( sector_t * sec, int platID, int type );

// Every floor start sound passes here, and exactly one command goes out: keyed on the sector
// to `client` while the gate is on, and otherwise on the floor to everyone, as stock sends it.
void MoverSendFloorSound( sector_t * sec, int floorID, unsigned int client = MAXPLAYERS,
	unsigned int flags = 0 );

// Ceiling and plat sounds this server sent, by which command carried them: the two must add
// up to every sound it played, and only one of them can be moving at a time.
void MoverSoundStats( unsigned int & bySector, unsigned int & legacy, bool bReset );

// The same counts per kind, for a window that zeroes them when its capture opens so that the
// counter and the wire cover one interval. SNAPSND_* indexes both arrays.
void MoverSoundStatsByKind( unsigned int * bySector, unsigned int * legacy, bool bReset );

// Tells one joining client the mover gate, if the snapshot is on at all.
void SendMoverGateTo(unsigned int client);
// True, and counted, when the gate stops a legacy send for this class (bPlane:
// a verification write, counted apart).
bool MoverServerStops(BYTE cls, bool bPlane = false);
void ServerMoverGateStats(unsigned int * stopped, unsigned int & planes,
	unsigned int & resent, unsigned int & tableResets, bool bReset);

// Once per pump, ahead of the capture.
void MoverTick();

}} // namespace zx::netsnapshot

struct sector_t;

namespace zx { namespace netsnapshot {

// A plane write from outside a mover thinker, behind fua_csnap_planetrace;
// `pFrom` is the sector that drove a propagated write, NULL for a direct one.
void TracePlaneWrite(sector_t * pSec, int plane, const char * pszWho,
	fixed_t before, fixed_t after, const sector_t * pFrom = NULL);

}} // namespace zx::netsnapshot

// Slots the capture table holds (7.1's 646 lifts, with room), here since the
// per-client pass sizes its arrays from it.
#define SNAPMV_MAXSLOTS			1024

namespace zx { namespace netsnapshot {

// Dropped with the level, since slot identity is a pointer.
void MoverReset();

// Reads one slot, false for a free slot or an unwritable class; `pBody` needs
// SNAPMV_BODY_MAX of room.
unsigned int MoverSlotCount();
bool MoverSlotRead(unsigned int slot, BYTE & cls, BYTE & flags,
	WORD & index, BYTE & gen, WORD & seq, BYTE * pBody, unsigned int & bodyBytes);

// The capture counters, `bumps` near `captures` meaning a compared plane.
void MoverCaptureStats(unsigned int & live, unsigned int & captures,
	unsigned int & bumps, unsigned int & full, unsigned int & blocked,
	unsigned int & recycled, unsigned int & recycledPlane,
	unsigned int & disowned, unsigned int & retired, unsigned int & narrowed,
	unsigned int & sweptUnstarted, unsigned int & sweptPressure,
	unsigned int & heldPast, unsigned int & heldLongest, unsigned int & heldNow,
	bool bReset);

// Whether a client's own MoveLocalPlayer and SetLocalPlayerJumpTics give way to
// the local block (level 3), never for a spectator.
bool SuppressesLegacyLocal(unsigned int client);

void BranchCensus(int distUnits, int staleTics, int velUnitsPerTic, bool bDeath);
void BranchReset();
void StraightReset();

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_CAPTURE_H
