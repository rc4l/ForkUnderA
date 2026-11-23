//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: record.h
//
// Description: The client-side snapshot recorder (.csnap) and its reader:
// payloads with their arrival times, drawn frames with the player's pose,
// the sampler's cvars, mirror appearance and map markers.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_RECORD_H
#define ZX_NETSNAPSHOT_RECORD_H

// What one client was told and when, beside the server's .zsnap (record.md).

#include <stdio.h>
#include "features/net-snapshot/format.h"

// For ESnapReadResult, the outcomes both fixture readers share.
#include "features/net-snapshot/stream.h"

#define CSNAP_MAGIC			"CSNP"
// Moves with every wire change a decoder cannot skip (history in record.md).
#define CSNAP_VERSION		18
#define CSNAP_VERSION_MIN	14

// Record kinds, by a kind byte, as deltas and frames interleave on two clocks.
enum ECsnapKind
{
	CSNAP_DELTA		= 1,	// a snapshot payload arrived
	CSNAP_FRAME		= 2,	// a frame was drawn
	CSNAP_RESET		= 3,	// ghost table cleared (map change, reconnect)

	// What a mirror drew, on change and strided by fua_csnap_recmirror.
	CSNAP_MIRROR	= 4,

	// The map the deltas after it belong to, its checksum and its frame.
	CSNAP_MAP		= 5,
};

struct CsnapPlayerPose
{
	fixed_t			x, y, z;
	angle_t			angle;
	int				pitch;
	bool			valid;			// false before the local player exists
};

// One recorded cvar, an int, as every setting the sampler reads is Int or Bool.
struct CsnapCvar
{
	char			name[40];
	int				value;
};

//*****************************************************************************
//	RECORDING (client)

namespace zx { namespace netsnapshot {

// True while a recording is open, so the hot paths can skip cheaply.
bool RecorderActive();

// Called from the packet path with the payload exactly as handed to
// ApplyDelta, and the same clock values.
void RecorderDelta(const BYTE* pData, unsigned int size, int serverTic, int gameTic,
	unsigned int arrivalMS);

// One mirror's drawn appearance, when it changed; cheap unless recording.
void RecorderMirror(unsigned int id, WORD qstate, WORD sprite, BYTE frame, BYTE tics);

// Called once per drawn frame, before sampling, with the render clock and the
// pose the tier logic will use.
void RecorderFrame(unsigned int renderMS, const CsnapPlayerPose& pose);

// Called when the ghost table is torn down, so a replay clears its table at
// the same point instead of carrying ids across it.
void RecorderReset(const char* pszReason);

void RecorderStopIfActive(const char* pszReason);

// The client joined a map (SnapJoin): names it in an active recording.
void RecorderMapJoined();

// The loaded level's checksum, as CSNAP_MAP carries it, or false outside one.
bool RecorderLevelChecksum(BYTE checksum[16]);

}} // namespace zx::netsnapshot

//*****************************************************************************
//	READING (harness)

struct CsnapReader
{
	FILE*			file;

	unsigned int	version;
	unsigned int	ticrate;
	unsigned int	flags;
	// Which player recorded this, from the header.
	int				localPlayer;

	// The header's cvar block, then the live set as of the last record read.
	CsnapCvar*		cvars;
	unsigned int	cvarCount;

	// Valid after a successful read.
	ECsnapKind		kind;
	int				serverTic;		// DELIVERY: counts pumps, not tics
	int				gameTic;		// ELAPSED WORLD TIME. DELTA only
	unsigned int	timeMS;			// arrival for DELTA, render for FRAME
	CsnapPlayerPose	pose;			// FRAME only
	const BYTE*		payload;		// DELTA only; points into the reader
	unsigned int	payloadSize;
	// MIRROR only: what the client drew, and how far into that state it was.
	unsigned int	mirrorId;
	WORD			mirrorQState;	// current only where local animation runs
	WORD			mirrorSprite;
	BYTE			mirrorFrame;
	BYTE			mirrorTics;
	// MAP only.
	char			mapName[64];
	BYTE			mapChecksum[16];
	SnapFrame		mapFrame;		// the narrow frame at origin 0 before v17
	bool			cvarsChanged;	// this record carried a new cvar block
	unsigned int	recordIndex;

	BYTE*			stored;
	unsigned int	storedCap;
	const char*		error;
	long			fileEnd;		// the file's size, -1 where ftell can't say
};

namespace zx { namespace netsnapshot {

bool CsnapOpen(CsnapReader& reader, const char* pszFilename);
ESnapReadResult CsnapReadRecord(CsnapReader& reader);
void CsnapClose(CsnapReader& reader);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_RECORD_H
