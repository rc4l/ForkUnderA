// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: localpred.cpp
//
// Description: The local player's block against the prediction it replaces.
// See localpred.h. Was carried in cl_pred.cpp in the reference, which had no
// module to put it in; here the stock file keeps one-line calls and every
// static, outcome and report line lives in this one.
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"			// CLIENT_PREDICTION_TICS
#include "doomstat.h"			// consoleplayer, gametic
#include "c_cvars.h"
#include "i_system.h"			// I_MSTime
#include "actor.h"
#include "d_player.h"
#include "cl_demo.h"			// CLIENTDEMO_IsPlaying, the buffered column
#include "cl_main.h"			// CLIENT_ApplyLocalBlock and its outcomes
#include "network.h"			// IDList, for this player's reserved id
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/localpred.h"

EXTERN_CVAR(Bool, cl_buffercommands)

// [Kaori-Chani] At global scope, as ghostactor.cpp defines them there beside the predict
// line that reports them.
extern unsigned long long g_predCalls, g_predTicsSum, g_predTicsMax;

// The reconciliation error census's interval in seconds, 0 off.
CVAR(Int, fua_csnap_predcensus, 0, CVAR_NOSETBYACS)

//*****************************************************************************
//	THE PREDICTION ERROR CENSUS

// Which tick each prediction slot holds, and whether it holds one at all.
//
// The ring is CLIENT_PREDICTION_TICS (70) deep and indexed by tick modulo, so a
// slot silently means a different tick every two seconds. Comparing without
// this is the id-reuse defect in another shape: an instrument keyed on a slot
// must be able to tell the slot was reused.
static unsigned int	g_predStampTick[CLIENT_PREDICTION_TICS];
static bool			g_predStampValid[CLIENT_PREDICTION_TICS];

// XY error in map units at the moment the server's position lands, bucketed
// <1, <4, <16, <64, 64+. Z is separate: a step or a lift is a legitimate
// disagreement of a different kind from being in the wrong place on the floor.
#define SNAP_PRED_BUCKETS	5
static unsigned int	g_predHistXY[SNAP_PRED_BUCKETS];
static unsigned int	g_predHistZ[SNAP_PRED_BUCKETS];
static unsigned int	g_predSamples = 0;
static unsigned int	g_predStale = 0;		// slot held a different tick
static unsigned int	g_predWorstXY = 0, g_predWorstZ = 0;
static QWORD		g_predDepthAcc = 0;
static unsigned int	g_predDepthSamples = 0, g_predDepthMax = 0;
static unsigned int	g_predLastReport = 0;
static bool			g_predHaveLastReport = false;

// Correction arrival, every periodic correction that lands, by tics since the
// previous one.
#define SNAP_PRED_GAPBUCKETS	6
static unsigned int	g_predGapHist[SNAP_PRED_GAPBUCKETS];
static unsigned int	g_predGapSamples = 0;
static unsigned int	g_predGapWorst = 0;

// Not `== 0`: I_MSTime can legitimately return a small value early in a
// session, and a zero-means-absent flag reads the first correction as a gap
// from the beginning of time.
static unsigned int	g_predLastCorrectionMS = 0;
static bool			g_predHaveLastCorrection = false;

// Where the ring cursor went between consecutive corrections, since a buffered
// position command can wind it backward rather than forward.
static unsigned int	g_predCursorBack = 0, g_predCursorSame = 0;
static unsigned int	g_predLastCursorTick = 0;
static bool			g_predHaveLastCursor = false;

// Two corrections applied within one gametic - the direct signature of the
// buffer draining its two-per-tick allowance.
static unsigned int	g_predSameTic = 0;
static int			g_predLastCorrectionTic = 0;

static int snap_PredBucket(fixed_t err)
{
	// [Kaori-Chani] The cast acts on the raw, as every zx::Fixed cast does, so
	// this is the whole-unit count the buckets are in.
	const unsigned int units = (unsigned int)(abs(err) >> FRACBITS);
	if (units < 1)	return 0;
	if (units < 4)	return 1;
	if (units < 16)	return 2;
	if (units < 64)	return 3;
	return 4;
}

// Half-tic edges around each tic multiple at 1000/35 ms per tic.
static int snap_PredGapBucket(unsigned int ms)
{
	if (ms < 14)	return 0;		// under half a tic: a same-tic pair
	if (ms < 43)	return 1;		// ~1 tic
	if (ms < 71)	return 2;		// ~2 tics
	if (ms < 100)	return 3;		// ~3 tics
	if (ms < 129)	return 4;		// ~4 tics
	return 5;
}

// The report below prints six gap columns by hand. Asserting against the
// constant rather than against a literal is what stops the next column being
// added to one of the two places.
typedef char snap_assert_predgap_columns[(SNAP_PRED_GAPBUCKETS == 6) ? 1 : -1];

//*****************************************************************************
//	THE LOCAL BLOCK

// The local player's block: the reconciliation reference with the authority
// bit, and a compared shadow without it.
struct SnapLocalShadow
{
	DWORD	clientTic;
	fixed_t	x, y, z;
	fixed_t	velx, vely, velz;
	SWORD	jumpTics;
	bool	valid;
};
static SnapLocalShadow	g_localShadow[CLIENT_PREDICTION_TICS];

static unsigned int	g_localBlocksAuth = 0;		// blocks received carrying the authority bit
static unsigned int	g_localBlocksShadow = 0;	// blocks received without it
static unsigned int	g_localCompared = 0;	// slots where both sources agreed to exist
static unsigned int	g_localNoLegacy = 0;	// block present, legacy never wrote the slot
static unsigned int	g_localDiffPos = 0, g_localDiffVel = 0, g_localDiffJump = 0;
static fixed_t		g_localWorstXY = 0, g_localWorstZ = 0;

// Every visit lands in a named outcome, checked at the report; `skipped`
// counts cursor tics never visited.
static unsigned int	g_localVisits = 0;		// LocalPredVisit calls
static unsigned int	g_localIdle = 0;		// visit while prediction is not running
static unsigned int	g_localRepeat = 0;		// cursor did not advance since last visit
static unsigned int	g_localAbsent = 0;		// fresh tick, no block for it
static unsigned int	g_localStale = 0;		// fresh tick, slot holds another tick's block
static unsigned int	g_localSkipped = 0;		// tics the cursor jumped OVER, never visited
static unsigned int	g_localOrphan = 0;		// block overwritten before any visit
static unsigned int	g_localLastVisit = 0;
static bool			g_localHaveLastVisit = false;

// Authoritative mode, following the wire bit rather than a cvar.
static bool			g_localAuthoritative = false;
static unsigned int	g_localAuthApplied = 0;
static unsigned int	g_localNoPawn = 0;
static unsigned int	g_localOutOfOrder = 0;
static unsigned int	g_localDead = 0;
static unsigned int	g_localSpectator = 0;
static unsigned int	g_localUnknownOutcome = 0;

// Whether the legacy command has written this slot since it was last compared.
// Without it a slot the block reached first would compare against whatever the
// ring held from 70 tics ago and report a divergence that is really an absence.
static bool			g_localLegacySeen[CLIENT_PREDICTION_TICS];

// Raised while this module's own applier runs, because it reaches the setter the
// legacy correction reaches and its write is not a legacy one.
static bool			g_localApplying = false;

// Per-process state, where the reset beside it is per map, so a second map
// change must not reinstall the sink.
static bool			g_localSinkInstalled = false;

// The wire's own id for this player, so a block addressed to someone else is
// counted rather than silently applied. It cannot happen today - the server
// builds one block per client and addresses it to that client - but a counter
// is what would say so.
static unsigned int	g_localWrongId = 0;

// The sink the module installs, which the apply layer calls with each local
// block. The STORE is unconditional, so a diagnostic does not decide whether
// the shadow exists; only the counters are gated, so their window is the
// report window.
static void snap_LocalSink(unsigned int id, const SnapLocalState & st, int gameTic)
{
	const bool bCount = (fua_csnap_predcensus > 0);

	const unsigned int mine =
		(unsigned int)IDList<AActor>::reservedIDForPlayer(consoleplayer);
	if ((mine == 0) || (id != mine))
	{
		if (bCount)
			++g_localWrongId;
		return;
	}

	// Counted per MODE, not in one total: the report picks its form from the
	// last block's flag, so a window in which the server's suppression
	// changed would print one mode's blocks against the other's counts.
	const bool bAuth = ((st.flags & SNAPLOCAL_AUTHORITATIVE) != 0);
	g_localAuthoritative = bAuth;
	if (bCount)
	{
		if (bAuth)
			++g_localBlocksAuth;
		else
			++g_localBlocksShadow;
	}

	if (bAuth)
	{
		// Applied in cl_main.cpp, beside the command it replaces, because two
		// of the side effects it must reproduce read file statics there
		// (g_ulFirstSpawnedTic, g_ulEndFullUpdateTic) and exposing them to get
		// the rule into this file would put the same rule in two places. The
		// outcome comes back as a code so the decision lives with its data and
		// the counting lives with the report.
		g_localApplying = true;
		const int outcome = CLIENT_ApplyLocalBlock(st, gameTic);
		g_localApplying = false;
		if (bCount)
		{
			switch (outcome)
			{
			case CLIENTLOCAL_APPLIED:		++g_localAuthApplied; break;
			case CLIENTLOCAL_NOPAWN:		++g_localNoPawn; break;
			case CLIENTLOCAL_OUTOFORDER:	++g_localOutOfOrder; break;
			case CLIENTLOCAL_DEAD:			++g_localDead; break;
			case CLIENTLOCAL_SPECTATOR:		++g_localSpectator; break;
			default:						++g_localUnknownOutcome; break;
			}
		}
		return;
	}

	SnapLocalShadow & sh = g_localShadow[st.clientTic % CLIENT_PREDICTION_TICS];

	// Still valid means the previous occupant was never visited: an orphan,
	// detected here because overwrite is the only moment its fate is decided.
	if (bCount && (sh.valid) && (sh.clientTic != st.clientTic))
		++g_localOrphan;

	sh.clientTic = st.clientTic;
	sh.x = st.x;	sh.y = st.y;	sh.z = st.z;
	sh.velx = st.velx;	sh.vely = st.vely;	sh.velz = st.velz;
	sh.jumpTics = st.jumpTics;
	sh.valid = true;
}

//*****************************************************************************
//	THE ENTRIES, ONE PER CALL FROM A STOCK FILE

void zx::netsnapshot::LocalPredInstall()
{
	if (g_localSinkInstalled)
		return;
	SetLocalSink(snap_LocalSink);
	g_localSinkInstalled = true;
}

void zx::netsnapshot::LocalPredReset()
{
	memset(g_predStampValid, 0, sizeof(g_predStampValid));
	memset(g_predStampTick, 0, sizeof(g_predStampTick));
	memset(g_predHistXY, 0, sizeof(g_predHistXY));
	memset(g_predHistZ, 0, sizeof(g_predHistZ));
	g_predSamples = g_predStale = 0;
	g_predWorstXY = g_predWorstZ = 0;
	g_predDepthAcc = 0;
	g_predDepthSamples = g_predDepthMax = 0;
	g_predLastReport = 0;
	g_predHaveLastReport = false;
	memset(g_predGapHist, 0, sizeof(g_predGapHist));
	g_predGapSamples = g_predGapWorst = 0;
	g_predHaveLastCorrection = false;
	g_predHaveLastCursor = false;
	g_predCursorBack = g_predCursorSame = g_predSameTic = 0;
	g_predLastCorrectionMS = 0;
	g_predLastCorrectionTic = 0;

	memset(g_localShadow, 0, sizeof(g_localShadow));
	memset(g_localLegacySeen, 0, sizeof(g_localLegacySeen));
	g_localHaveLastVisit = false;
	g_localLastVisit = 0;
	g_localApplying = false;
}

void zx::netsnapshot::LocalPredLegacyWrote(unsigned int tick)
{
	// Not behind the census gate: off, every comparison would read `no legacy`.
	// Our own applier reaches the same setter, and its write is not a legacy one.
	if (g_localApplying)
		return;
	g_localLegacySeen[tick % CLIENT_PREDICTION_TICS] = true;
}

void zx::netsnapshot::LocalPredArrival()
{
	if (fua_csnap_predcensus <= 0)
		return;

	const unsigned int tick = CLIENT_GetLastConsolePlayerUpdateTick();
	const unsigned int nowMS = I_MSTime();

	if (g_predHaveLastCorrection)
	{
		const unsigned int gap = nowMS - g_predLastCorrectionMS;
		++g_predGapHist[snap_PredGapBucket(gap)];
		++g_predGapSamples;
		if (gap > g_predGapWorst)
			g_predGapWorst = gap;
	}
	g_predLastCorrectionMS = nowMS;
	g_predHaveLastCorrection = true;

	if (g_predHaveLastCursor)
	{
		if (tick < g_predLastCursorTick)
			++g_predCursorBack;
		else if (tick == g_predLastCursorTick)
			++g_predCursorSame;

		if (gametic == g_predLastCorrectionTic)
			++g_predSameTic;
	}
	g_predLastCursorTick = tick;
	g_predHaveLastCursor = true;
	g_predLastCorrectionTic = gametic;
}

void zx::netsnapshot::LocalPredError(unsigned int tick, const fixed_t landed[3],
	const fixed_t base[3])
{
	if (fua_csnap_predcensus <= 0)
		return;

	// The error, measured at the one instant both values exist: the ring still
	// holds what this client PREDICTED for this tick, and the arguments carry
	// what the server says actually happened. A tick the client never predicted
	// is not an error, so it is counted apart rather than scored as a perfect
	// one - a zero from an absent comparison and a zero from an exact
	// prediction must not look alike.
	const unsigned int slot = tick % CLIENT_PREDICTION_TICS;
	if ((g_predStampValid[slot] == false) || (g_predStampTick[slot] != tick))
	{
		++g_predStale;
		return;
	}

	const fixed_t dx = base[0] - landed[0];
	const fixed_t dy = base[1] - landed[1];
	const fixed_t dz = base[2] - landed[2];

	// Manhattan rather than Euclidean: this is a magnitude bucket, and a square
	// root per correction buys nothing a bucket edge can see.
	const fixed_t exy = abs(dx) + abs(dy);

	++g_predHistXY[snap_PredBucket(exy)];
	++g_predHistZ[snap_PredBucket(dz)];
	++g_predSamples;

	if ((unsigned int)(abs(exy) >> FRACBITS) > g_predWorstXY)
		g_predWorstXY = (unsigned int)(abs(exy) >> FRACBITS);
	if ((unsigned int)(abs(dz) >> FRACBITS) > g_predWorstZ)
		g_predWorstZ = (unsigned int)(abs(dz) >> FRACBITS);
}

void zx::netsnapshot::LocalPredStamp(unsigned int tick)
{
	const unsigned int slot = tick % CLIENT_PREDICTION_TICS;
	g_predStampTick[slot] = tick;
	g_predStampValid[slot] = true;
}

// Compare the block against what the legacy command left in the ring, at the
// one point both writers have had their chance: the slot the replay is about
// to read. Here rather than in the sink because a comparison belongs where the
// values are FINAL, and the two arrive in either order.
void zx::netsnapshot::LocalPredVisit(unsigned int baseTick, bool bIdle,
	const fixed_t pos[3], const fixed_t vel[3], int jumpTics)
{
	const bool bCount = (fua_csnap_predcensus > 0);
	const unsigned int slot = baseTick % CLIENT_PREDICTION_TICS;
	SnapLocalShadow & sh = g_localShadow[slot];

	if (bCount)
	{
		++g_localVisits;
		// Tics the cursor jumped over are never visited, so their blocks can
		// only become orphans: a lost block, or two landing between visits.
		if (g_localHaveLastVisit && (baseTick > g_localLastVisit + 1))
			g_localSkipped += (baseTick - g_localLastVisit - 1);
	}
	const bool bRepeat = (g_localHaveLastVisit && (baseTick == g_localLastVisit));
	g_localLastVisit = baseTick;
	g_localHaveLastVisit = true;

	if (bIdle)
	{
		if (bCount)
			++g_localIdle;
		sh.valid = false;
		return;
	}
	// A second visit to the same tick has already consumed its block, so an
	// empty slot here says nothing about delivery. Separated from `absent` or
	// the two would share a total while describing different things.
	if (bRepeat)
	{
		if (bCount)
			++g_localRepeat;
		return;
	}
	if (sh.valid == false)
	{
		if (bCount)
			++g_localAbsent;
		return;
	}
	// A block for a different tick occupying this slot is a ring wrap, not a
	// disagreement. Consume it either way so it cannot be compared twice.
	if (sh.clientTic != baseTick)
	{
		if (bCount)
			++g_localStale;
		sh.valid = false;
		return;
	}
	if (g_localLegacySeen[slot] == false)
	{
		if (bCount)
			++g_localNoLegacy;
		sh.valid = false;
		return;
	}

	if (bCount)
	{
		++g_localCompared;
		const fixed_t dx = abs(sh.x - pos[0]);
		const fixed_t dy = abs(sh.y - pos[1]);
		const fixed_t dz = abs(sh.z - pos[2]);
		const fixed_t dxy = (dx > dy) ? dx : dy;
		if (dxy > g_localWorstXY)	g_localWorstXY = dxy;
		if (dz > g_localWorstZ)		g_localWorstZ = dz;
		if ((dxy != 0) || (dz != 0))
			++g_localDiffPos;
		if ((sh.velx != vel[0]) || (sh.vely != vel[1]) || (sh.velz != vel[2]))
			++g_localDiffVel;
		if ((int)sh.jumpTics != jumpTics)
			++g_localDiffJump;
	}

	sh.valid = false;
	g_localLegacySeen[slot] = false;
}

// From PlayerPredict, where the replay depth is decided: the depth sample and
// the timed report. Depth is reported beside the error because the two move
// together - a correction after eight replayed tics has had eight tics to
// diverge, so an error figure without a depth beside it cannot be compared
// across pings.
void zx::netsnapshot::LocalPredSample(unsigned int predictionTics)
{
	// [Kaori-Chani] Ahead of the census gate: the client's predict line reports these every
	// window, whether or not the detailed census is on.
	{
		++g_predCalls;
		g_predTicsSum += predictionTics;
		if (predictionTics > g_predTicsMax)
			g_predTicsMax = predictionTics;
	}

	if (fua_csnap_predcensus <= 0)
		return;

	g_predDepthAcc += predictionTics;
	++g_predDepthSamples;
	if (predictionTics > g_predDepthMax)
		g_predDepthMax = predictionTics;

	const unsigned int nowMS = I_MSTime();
	if (g_predHaveLastReport == false)
	{
		g_predLastReport = nowMS;
		g_predHaveLastReport = true;
		return;
	}
	if ((nowMS - g_predLastReport) < (unsigned int)(fua_csnap_predcensus * 1000))
		return;

	if (g_predSamples > 0)
	{
		const double inv = 100.0 / (double)g_predSamples;
		Printf("[snap] reconcile: %u corrections | XY <1u %.0f%% <4 %.0f%% <16 %.0f%% "
			"<64 %.0f%% 64+ %.0f%% (worst %u) | Z 64+ %.0f%% (worst %u) | depth avg %.1f "
			"max %u | %u uncomparable\n",
			g_predSamples,
			g_predHistXY[0] * inv, g_predHistXY[1] * inv, g_predHistXY[2] * inv,
			g_predHistXY[3] * inv, g_predHistXY[4] * inv, g_predWorstXY,
			g_predHistZ[4] * inv, g_predWorstZ,
			(g_predDepthSamples > 0)
				? ((double)(QWORD)g_predDepthAcc / (double)g_predDepthSamples) : 0.0,
			g_predDepthMax, g_predStale);
	}
	// Every periodic correction, on its own line.
	if (g_predGapSamples > 0)
	{
		const double ginv = 100.0 / (double)g_predGapSamples;
		Printf("[snap] arrival: %u gaps | <1t %.0f%% 1t %.0f%% 2t %.0f%% 3t %.0f%% "
			"4t %.0f%% 5t+ %.0f%% (worst %u ms) | cursor back %u same %u | "
			"2-in-tic %u | buffered %d\n",
			g_predGapSamples,
			g_predGapHist[0] * ginv, g_predGapHist[1] * ginv, g_predGapHist[2] * ginv,
			g_predGapHist[3] * ginv, g_predGapHist[4] * ginv, g_predGapHist[5] * ginv,
			g_predGapWorst, g_predCursorBack, g_predCursorSame, g_predSameTic,
			(CLIENTDEMO_IsPlaying() ? (CLIENTDEMO_IsUsingCommandBuffers() ? 1 : 0)
				: (cl_buffercommands ? 1 : 0)));
	}
	// The block's line, divergence reading zero.
	if ((g_localBlocksAuth | g_localBlocksShadow | g_localCompared | g_localVisits) != 0)
	{
		unsigned int loApplied = 0, loNoSink = 0;
		ClientLocalStats(loApplied, loNoSink, true);
		// Blocks in the OTHER mode mean the server's suppression changed
		// inside this window, so both halves are partial.
		if (g_localAuthoritative)
		{
			// The divergence columns are NOT printed here, and that is the
			// point. In this mode the block wrote the ring, so comparing it
			// against the ring compares it with itself and yields a zero that
			// means nothing - indistinguishable on the page from the verified
			// zero the shadow mode produces.
			const unsigned int parts = g_localAuthApplied + g_localNoPawn
				+ g_localOutOfOrder + g_localDead + g_localSpectator
				+ g_localUnknownOutcome;
			Printf("[snap] local block: mode AUTHORITATIVE | %u blocks, %u applied"
				" | %u no-pawn | %u out-of-order | %u dead | %u spectator"
				" | %u wrong-id | %u no-sink | %u unknown%s%s\n",
				g_localBlocksAuth, g_localAuthApplied, g_localNoPawn,
				g_localOutOfOrder, g_localDead, g_localSpectator,
				g_localWrongId, loNoSink, g_localUnknownOutcome,
				(parts == g_localBlocksAuth) ? "" : "  ** UNACCOUNTED **",
				(g_localBlocksShadow > 0) ? "  (mode changed mid-window)" : "");
		}
		else
		{
			Printf("[snap] local block: mode shadow | %u blocks, %u cmp"
				" | pos %u vel %u jump %u | worst XY %u Z %u"
				" | %u wrong-id | %u no-sink%s\n",
				g_localBlocksShadow, g_localCompared,
				g_localDiffPos, g_localDiffVel, g_localDiffJump,
				(unsigned int)(g_localWorstXY >> FRACBITS),
				(unsigned int)(g_localWorstZ >> FRACBITS),
				g_localWrongId, loNoSink,
				(g_localBlocksAuth > 0) ? "  (mode changed mid-window)" : "");
		}

		// Every visit in one named outcome, so a missing one shows as a total
		// that does not add up rather than as a quiet omission.
		const unsigned int parts = g_localIdle + g_localRepeat + g_localAbsent
			+ g_localStale + g_localNoLegacy + g_localCompared;
		Printf("[snap] local attrib: %u visits = %u cmp + %u idle + %u repeat"
			" + %u absent + %u stale + %u no-legacy%s | %u skipped | %u orphan\n",
			g_localVisits, g_localCompared, g_localIdle, g_localRepeat,
			g_localAbsent, g_localStale, g_localNoLegacy,
			(parts == g_localVisits) ? "" : "  ** UNACCOUNTED **",
			g_localSkipped, g_localOrphan);
	}

	g_predLastReport = nowMS;
	g_predHaveLastReport = true;
	memset(g_predHistXY, 0, sizeof(g_predHistXY));
	memset(g_predHistZ, 0, sizeof(g_predHistZ));
	g_predSamples = g_predStale = 0;
	g_predWorstXY = g_predWorstZ = 0;
	g_predDepthAcc = 0;
	g_predDepthSamples = g_predDepthMax = 0;
	memset(g_predGapHist, 0, sizeof(g_predGapHist));
	g_predGapSamples = g_predGapWorst = 0;
	g_localBlocksAuth = g_localBlocksShadow = 0;
	g_localCompared = g_localNoLegacy = 0;
	g_localDiffPos = g_localDiffVel = g_localDiffJump = 0;
	g_localWorstXY = g_localWorstZ = 0;
	g_localWrongId = 0;
	g_localVisits = g_localIdle = g_localRepeat = 0;
	g_localAbsent = g_localStale = g_localSkipped = g_localOrphan = 0;
	g_localAuthApplied = g_localNoPawn = g_localOutOfOrder = 0;
	g_localDead = g_localSpectator = g_localUnknownOutcome = 0;
}
