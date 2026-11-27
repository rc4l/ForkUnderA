// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: report.h
//
// Description: The [snap] stats report: what the pump calls every tick, and
// what the capture keeps for the report to print. The report is report.cpp.
//
//-----------------------------------------------------------------------------

#ifndef ZX_NETSNAPSHOT_REPORT_H
#define ZX_NETSNAPSHOT_REPORT_H

#include <stdio.h>		// FILE, the recorder's handle a report line names
#include <string.h>		// memset, which the totals reset with

#include "basictypes.h"	// GCCPRINTF
#include "doomdef.h"	// MAXPLAYERS
#include "tarray.h"
#include "i_tictime.h"	// TIC_ScaleRaw: every figure in the report passes through it
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/clienttrack.h"	// SNAPCL_TIERS_REPORT, the bands it prints

class AActor;

// Each class's verdict, taken once a map (design doc 6.3, Classification).
enum
{
	SNAPCOS_UNKNOWN = 0,
	SNAPCOS_INERT,			// passed the inert test (flags ignored)
	SNAPCOS_NETWORKED,		// failed it; why and what in the two tables
	SNAPCOS_VISITING,		// being classified (a spawn reaching it is a cycle)
};

// The cosmetic event command's header (SVC2 2, map stamp 1, count 2).
#define SNAP_COSEVENT_HEADER	5

// How many entering classes the report names, the enters re-sent over the ack
// round trip being a large share of the budget.
#define SNAP_ENTERCLASS_REPORT		6

// The events note's table. +NONETID only says "no NetID", so it takes a row of
// its own only when the class is not inert.
enum
{
	SNAPCOS_ROW_NONE = 0,
	SNAPCOS_ROW_GONE,			// +CLIENTSIDEONLY, inert: no server copy past the flush
	SNAPCOS_ROW_KEPT,			// +CLIENTSIDEONLY, not inert: server copy kept
	SNAPCOS_ROW_INERT,			// inert, not +CLIENTSIDEONLY
	SNAPCOS_ROW_PLAIN,			// +NONETID, not inert: a plain NetID-0 client copy
	SNAPCOS_ROW_BLOOD,			// a blood spawn whose class is in no row above
	SNAPCOS_ROWS
};

namespace zx { namespace netsnapshot {

// Accumulated over the reporting window.
struct SnapStats
{
	unsigned int frames;
	QWORD simTics;			// gametics covered by these captures; >frames = server saturated
	QWORD entities, awake, settled, enters, leaves;
	QWORD deltaBytes, zstdBytes, dirtyRecords;
	QWORD animOnly;			// dirty records whose only change was SNAPGRP_APPEAR

	// Dirty records and their payload bytes per group, in snap_TallyGroups'
	// order (POS, POSZ, ANG, VELMISC, APPEAR, RENDER).
	QWORD grpRecs[SNAP_GROUP_COUNT];
	QWORD grpBytes[SNAP_GROUP_COUNT];

	unsigned int peakDelta, peakZstd, peakEntities;

	void Reset() { memset(this, 0, sizeof(*this)); }
};

// Once a pump, from ServerTick: the window's lines are queued whole, and one of
// them is printed a pump, since printing the report takes longer than a tic.
void snap_MaybeReport();
void snap_DrainOneLine();

// A line the server's operator must see, on the console as well as in the log.
void snap_Warn(const char* fmt, ...) GCCPRINTF(1, 2);

// The raw counter in milliseconds, inline so both files read a figure the same way.
inline double snap_RawToMS(QWORD raw)
{
	return (double)(QWORD)TIC_ScaleRaw((tictime_raw_t)raw, 1000000) / 1000.0;
}

//*****************************************************************************
//	WHAT THE CAPTURE KEEPS FOR IT
//
// Counted in capture.cpp, beside the code they count, and declared here rather
// than moved so an edit to a report line cannot relay the capture's own code.

extern unsigned int g_targetEnters, g_targetNoneEnters;
extern unsigned int g_capEffectsActors;
extern BYTE g_capEffectsBits;
extern SnapFrame g_mapFrame;
extern unsigned int g_frameClamps;
extern BYTE g_snapMapStamp;
extern BYTE* g_cosVerdict;	// by classId
extern double g_cosClassifyMS;	// the last full pass
extern unsigned int g_cosAcsClasses, g_cosAcsRow1;
extern unsigned int* g_cosInheritByClass;	// networked children, by class
extern unsigned int g_cosInheritTotal;	// networked children
extern unsigned int g_cosInheritOverlap;	// cosmetic children: already layer 1
extern TArray<AActor*> g_ceHeld;	// for the deadline
extern QWORD g_ceGeneric[SNAPCOS_ROWS];	// generic events by row
extern QWORD g_ceBlood, g_ceSplatter;
extern QWORD g_ceByCopy, g_ceDiedEarly, g_ceUntold, g_ceFromTick;
extern QWORD g_ceVelBig, g_ceVelSmall, g_ceStateByte, g_ceStateOutside;
extern QWORD g_ceExtras, g_ceRelaxed, g_ceRemoved;
extern unsigned int* g_ceNotInertByClass;	// rows 2 and 4, by class
extern QWORD g_ceClientTics, g_ceKept, g_ceCulled, g_ceShooterSkips;
extern QWORD g_ceWanted, g_ceSent, g_ceCapDropped, g_cePumpsCapped;
extern QWORD g_ceClientPumps, g_ceAppended, g_ceAlone;
extern QWORD g_ceAgeSaturated, g_ceBloodKept, g_ceBloodUnknown;
extern QWORD g_ceTodayRecs[SNAPCOS_ROWS], g_ceTodayBytes[SNAPCOS_ROWS];
extern QWORD g_ceCarried, g_ceCarryAged, g_ceCarryOver, g_ceBankAtCap;
extern QWORD g_ceHeldN, g_ceRelTick, g_ceRelScript, g_ceRelDeadline;
extern QWORD g_ceRemovedHeld, g_ceScriptAfter;
extern QWORD g_ceTidRule[4], g_ceOrphaned, g_ceAcsKept;
extern QWORD g_ceAcsRouted, g_ceAcsClientSide;	// routed ACS spawns
extern QWORD g_ceJoins, g_ceJoinAlive, g_ceJoinWorst;
extern unsigned int* g_ceAgeByClass;	// scratch for the report
extern unsigned int g_cmNetworked, g_cmListed, g_cmLive, g_cmSolidOnly;
extern unsigned int g_cmRefused[4];	// marker, tid, special, args
extern unsigned int* g_cmListedByClass;
extern unsigned int* g_cmSolidByClass;
extern QWORD g_cmTodayRecs, g_cmTodayBytes;
extern QWORD g_cmSolidRecs, g_cmSolidBytes;
extern QWORD g_phaseCaptureRaw;	// world diff + the capture census
extern QWORD g_phaseSendRaw;	// per-client priority pass and emit
extern QWORD g_phaseTickRaw;	// both, plus the rest of ServerTick
extern QWORD g_phaseWorstRaw;	// worst single tick in the window
extern QWORD g_pumpGapWorstRaw;
extern int g_pumpGapWorstTics;
extern QWORD g_pumpGapSumRaw;
extern unsigned int g_pumpGapCount;
extern QWORD g_reportRaw;
extern unsigned int g_phaseFrames;
extern QWORD g_chunkDatagrams;
extern QWORD g_chunkFrames;
extern unsigned int g_droppedPackets;
extern unsigned int g_heldPackets;
extern unsigned int g_reentryCount;
extern unsigned int g_reentryReuse;	// slots whose occupant changed
extern unsigned int g_branchCount;	// world-wide, once per branch per tic
extern unsigned int g_brDist[6];	// <512, 1k, 2k, 4k, 8k, beyond
extern unsigned int g_brStale[5];	// <2, <5, <10, <35, 35+ tics since position
extern unsigned int g_brVel[3];	// still, <8 units/tic, 8+
extern unsigned int g_brDeaths;
extern unsigned int g_brDeathsNear;	// deaths inside 512 units
extern unsigned int g_brTotal;
extern unsigned int g_runMissile[5];
extern unsigned int g_runOther[5];
extern unsigned int g_predHeld;	// bit set, heading held: extrapolation was right
extern unsigned int g_predTurned;	// bit set, heading turned: extrapolated into a turn
extern unsigned int g_stepHeld;	// bit clear, heading held: stepped something straight
extern unsigned int g_stepTurned;	// bit clear, heading turned: stepped correctly
extern unsigned int g_straightSamples;
extern QWORD g_huntingAccum;
extern QWORD g_liveAccum;
extern QWORD g_ownedAccum;
extern unsigned int g_huntingFrames;
extern unsigned int* g_enterByClass;	// indexed by classId
extern unsigned int* g_popByClass;
extern unsigned int* g_popSettledByClass;
// Sets marked to be told, and sets told to a client.
extern unsigned int g_propMarked, g_propsTold;
extern unsigned int g_popTotal;
extern unsigned int g_popSettledTotal;
extern unsigned int g_enterClassTotal;
extern SnapStats g_stats;
extern FILE* g_recordFile;
extern unsigned int g_recordRecords;
extern QWORD g_recordBytes;
extern QWORD g_sentBytes;
extern QWORD g_tierAccum[SNAPCL_TIERS_REPORT];
extern QWORD g_forcedAccum;
extern QWORD g_cosmeticAccum;
extern QWORD g_deferredAccum;
extern unsigned int g_priorityFrames;
extern unsigned int g_sentPackets;
extern QWORD g_wirePayloadBytes;	// uncompressed, as handed to the transport
extern QWORD g_wireSentBytes;	// after Huffman/Zstandard
extern unsigned int g_wirePackets;
extern unsigned int g_wireZstdPackets;
extern unsigned int g_wirePeak;
extern QWORD g_wireLaunchRaw;	// [Kaori-Chani] the launches' time, compression included
extern QWORD g_freshAccum;
extern QWORD g_resendAccum;
extern QWORD g_resendByteAccum;
extern QWORD g_tierSizeAccum[SNAPCL_TIERS_REPORT];
extern QWORD g_resAllowAccum, g_resSpentAccum;
extern QWORD g_resCappedAccum, g_resDrainedAccum;
extern BYTE g_soundIdsAnswer[MAXPLAYERS];
extern unsigned int g_soundSent, g_soundCulled, g_soundPositional;
extern unsigned int g_soundRoutedPoint;	// NetID-less routed cosmetics, as points
extern unsigned int g_soundById, g_soundByName;
extern unsigned int g_soundNameRange, g_soundNameChannel;
extern unsigned int g_soundNameOff, g_soundNameUnmatched;
extern unsigned int g_soundUnplayable;

// And what the report asks the capture for, each of them cold: once a window.
void snap_CosEnsure();
BYTE snap_CosRow(unsigned int id);
bool snap_CosIsCosmetic(unsigned int id);
void snap_CosCountersReset();
unsigned int snap_CosCountAlive(unsigned int* pPast5, unsigned int* pPast30, unsigned int* pTidNow);
void snap_CatchUpStats(unsigned int& dropped, unsigned int& pumps, unsigned int& worst,
	unsigned int& runs, int& worstPercent);
unsigned int snap_PacketRoom();
unsigned int snap_EffectiveBudget();
unsigned int snap_ChunkLimit();
bool snap_ClientInMap(unsigned int client);

}} // namespace zx::netsnapshot

#endif // ZX_NETSNAPSHOT_REPORT_H
