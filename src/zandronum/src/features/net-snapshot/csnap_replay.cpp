//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: csnap_replay.cpp
//
// Description: The replay harness for client recordings (fua_csnap_analyze):
// a .csnap through the real decoder and sampler, reported per entity, with
// the glide tripwire and the drawn-state comparison.
//
//-----------------------------------------------------------------------------

// The real decoder and sampler on a recording, never a copy (cl_replay.md):
//
//   fua_csnap_analyze <file>              summary over the whole stream
//   fua_csnap_analyze <file> <id>         per-frame trace for one entity
//   fua_csnap_analyze <file> csv <out>    per-entity summary as CSV
//   fua_csnap_analyze <file> ... refill   any of the above, as plan C draws it
//   fua_csnap_analyze <file> ... backdate refill, a mid-tic entry backdated
//   fua_csnap_analyze <file> ... anchor   refill, starts anchored to the fold

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include "doomdef.h"
#include "doomstat.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "tarray.h"
#include "zstring.h"
#include "features/net-snapshot/record.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/computation/smooth_compute.h"	// ComputeSmooth
#include "features/net-snapshot/computation/sample_compute.h"	// ComputeHermite
#include "features/net-snapshot/computation/replay_compute.h"
#include "features/net-snapshot/capture.h"	// StateFromQState: the shared resolver
#include "features/net-snapshot/ghostactor.h"
#include "info.h"
#include "network.h"

using namespace zx::netsnapshot;

// The window sustained rate is judged over, several P_TryMove steps long.
#define GLIDE_WINDOW_MS 250

// Class ids come from getActorNetworkIndex, a small dense index.
#define CLASSRATE_MAX 4096

// Rows of the class rate table: the most-sampled classes, and only those
// with enough served windows to mean anything.
#define CLASSRATE_ROWS 8
#define CLASSRATE_MINWINS 200

// Plausible sustained rate as a percentage of the class's Speed per tic, by the
// missile bits: a missile moves Speed every tic, a walker once per chase state.
CVAR(Int, fua_csnap_glide_missile, 125, CVAR_NOSETBYACS)
CVAR(Int, fua_csnap_glide_monster, 55, CVAR_NOSETBYACS)

// The mirror's smoothing settings (cl_ghostactor.cpp), which the replay applies.
EXTERN_CVAR(Int, fua_csnap_smooth_step)
EXTERN_CVAR(Int, fua_csnap_smooth_nonballistic)
EXTERN_CVAR(Int, fua_csnap_smooth_ballistic)
EXTERN_CVAR(Int, fua_csnap_smooth_entities)

// Position records kept per entity for the accuracy check, and how many tics a
// drawn frame waits for the record after it before it counts as unresolved.
#define REPLAY_ACC_RECS		4
#define REPLAY_ACC_WAIT_TICS	70

// Per-entity accumulation over a replay: the live diagnostics give the
// aggregates, and a defect is located per entity.
struct ReplayEntity
{
	unsigned int	samples;
	unsigned int	drawn;

	// What the game drew, from CSNAP_MIRROR, against what this replay computes.
	unsigned int	drawnRecords;	// frames compared against a recorded appearance
	unsigned int	drawnAgree;		// ... and the replay produced the same sprite
	WORD			lastSprite;		// newest recorded drawn appearance
	BYTE			lastFrame;
	bool			haveDrawn;
	unsigned int	jumps;			// single-frame displacement over the bound
	unsigned int	worstJump;
	unsigned int	delaySumMS;
	unsigned int	delayMaxMS;

	// Implausible sustained motion: displacement over GLIDE_WINDOW_MS against
	// the class's bound for the elapsed tics.
	unsigned int	winStartMS;
	fixed_t			winStartX, winStartY, winStartZ;
	bool			winOpen;

	// Split by whether an update landed inside the window, which alone can
	// blame the sampler.
	unsigned int	winServed, overServed;		// updates arrived in-window
	unsigned int	winStarved, overStarved;	// none did
	unsigned int	worstServedPct, worstStarvedPct;
	// The class each worst window was recorded under: an id is reused within
	// a segment, so classId is its last occupant's, not the worst window's.
	unsigned short	worstServedClass, worstStarvedClass;
	unsigned int	overRun;
	unsigned int	glideEvents;				// sustained, served windows only

	unsigned short	classId;		// the class of the newest window

	fixed_t			lastX, lastY, lastZ;
	unsigned int	lastMS;
	bool			hasLast;

	// The last drawn pose while ballistic, for the segment's draw error.
	fixed_t			balX, balY, balZ, balVX, balVY, balVZ;
	unsigned int	balMS;
	bool			balHave;

	// Its newest position records, oldest first, for the accuracy check, and
	// a count bumped on each enter, so a reused id's waiting frames drop out.
	int				recTic[REPLAY_ACC_RECS];
	fixed_t			recX[REPLAY_ACC_RECS], recY[REPLAY_ACC_RECS], recZ[REPLAY_ACC_RECS];
	fixed_t			recVX[REPLAY_ACC_RECS], recVY[REPLAY_ACC_RECS], recVZ[REPLAY_ACC_RECS];
	unsigned int	recCount;
	unsigned int	serial;

	SnapSmooth		smooth;			// the mirror's smoothing, run on the sampler's pose
};

// A drawn ballistic frame, or with bBody a body's, waiting for the record after
// its model time.
struct ReplayAccWait
{
	unsigned int	id, serial;
	double			tic;
	fixed_t			x, y, z;
	bool			bBody;
};

// One ghost's poses from its last plan C refill: the smoothed start and end of
// the client tic, and the end's full pose.
struct ReplayRefill
{
	fixed_t			sx, sy, sz;
	fixed_t			ex, ey, ez;
	SnapGhostPose	end;
	bool			ok;
	bool			drawn;		// drawn from these, so a mid-tic refill starts where it is
	bool			noStart;	// the ghost was not drawable at the tic's start
	bool			backdated;	// ... and its start was set a span of its velocity behind its end
	bool			anchored;	// a mid-tic refill set the start so the frame drew the fold
	bool			snapped;	// a mid-tic refill's fold snapped or reset, a label for the reports
	const char*		startExit;	// the sampler's exits for the two poses
	const char*		endExit;
	const char*		hereExit;	// ... and for the pose at the refill's frame

	// The trace's refill line in fixed units (h-E, S-E, E-raw).
	fixed_t			hMinusE[3], sMinusE[3], eMinusRaw[3];
	SnapSmooth		base;		// its smoothing at its latest refill, kept within the tic
	SnapSmoothStats	counted;	// the drawn counts the tic's latest refill added to the census
};

// Observed sustained rate per class over a segment's served windows, as a
// percentage of Speed per tic, by class id.
struct ReplayClassRate
{
	QWORD			pctSum;
	unsigned int	wins;
	unsigned int	pctPeak;
	bool			bMissile;		// a served window carried a missile bit
};

// One segment of a replay: the span between cvar changes, so an A/B
// recorded by flipping a setting mid-stream reports each half separately.
struct ReplaySegment
{
	unsigned int	index;
	unsigned int	startMS, endMS;
	unsigned int	frames;

	// Ballistic draw error: between two frames at one velocity a projectile
	// should move velocity x elapsed, so the miss is what the sampler added.
	unsigned int	balSteps, balOver8, balOver32;
	double			balErrSum, balErrMax;
	unsigned int	balHist[SNAP_REPLAY_BINS];

	// Ballistic accuracy against where the server had it at the frame's model
	// time, frames before a record at rest apart.
	unsigned int	accSteps, accOver8, accOver32, accUnresolved;
	double			accErrSum, accErrMax;
	unsigned int	accHist[SNAP_REPLAY_BINS];
	unsigned int	accRestSteps;
	double			accRestErrSum;

	// Body accuracy, the default path only: a body with a wire velocity against
	// the straight line between the records either side.
	unsigned int	bodyAccSteps, bodyAccOver8, bodyAccOver32, bodyAccUnresolved;
	double			bodyAccErrSum, bodyAccErrMax;
	unsigned int	bodyAccHist[SNAP_REPLAY_BINS];

	SnapSmoothStats	smooth;			// the live [snap] smoothing census, over the segment

	unsigned int	refillsTic, refillsDelta;	// plan C refills, by what made each due
	unsigned int	refillsNoStart, refillsBackdated;	// ghost refills with no start pose
	unsigned int	refillsAnchored, refillsSnapped;	// ghost refills anchored, and after a snap

	// The worst draw-error step, to trace, with its exits and refill.
	unsigned int	balWorstId, balWorstMS, balWorstSince;
	unsigned short	balWorstClass;
	const char*		balWorstExit;
	const char*		balWorstStart;
	bool			balWorstBackdated;
	bool			balWorstAnchored;
	bool			balWorstSnapped;
	char			balWorstRefill;
};

// Recorded cvars this build no longer has, with the constant each became, or
// REPLAY_UNREAD for one nothing read.
#define REPLAY_UNREAD INT_MIN
struct ReplayRetiredCvar { const char* name; int value; };
static const ReplayRetiredCvar g_replayRetired[] =
{
	{ "snap_extrap", 0 },
	{ "snap_anim_resync", 1000 },
	{ "snap_jumpsnap", 96 },
	{ "snap_velextrap", CLENT_VELEXTRAP_MS },
	{ "snap_gaprate", REPLAY_UNREAD },
	{ "clsnap_gaprate", REPLAY_UNREAD },
	{ "clsnap_body_extrap", 2 },
};

// Recorded names this build renamed, from either earlier prefix, `snap_` or
// `clsnap_`, to the live name in one step.
struct ReplayRenamedCvar { const char* oldName; const char* newName; };
static const ReplayRenamedCvar g_replayRenamed[] =
{
	{ "snap_rendercull", "fua_csnap_rendercull" },
	{ "snap_localanim", "fua_csnap_localanim" },
	{ "snap_cull_near", "fua_csnap_cull_near" },
	{ "snap_cull_far", "fua_csnap_cull_far" },
	{ "snap_missile_extrap", "fua_csnap_missile_extrap" },
	{ "snap_recmirror", "fua_csnap_recmirror" },
	{ "clsnap_rendercull", "fua_csnap_rendercull" },
	{ "clsnap_localanim", "fua_csnap_localanim" },
	{ "clsnap_cull_near", "fua_csnap_cull_near" },
	{ "clsnap_cull_far", "fua_csnap_cull_far" },
	{ "clsnap_missile_extrap", "fua_csnap_missile_extrap" },
	{ "clsnap_age_servertime", "fua_csnap_age_servertime" },
	{ "clsnap_age_maxjitter", "fua_csnap_age_maxjitter" },
	{ "clsnap_smooth_step", "fua_csnap_smooth_step" },
	{ "clsnap_smooth_extrap", "fua_csnap_smooth_nonballistic" },
	{ "clsnap_smooth_missile", "fua_csnap_smooth_ballistic" },
	{ "clsnap_smooth_pop", "fua_csnap_smooth_entities" },
	{ "clsnap_recmirror", "fua_csnap_recmirror" },
};

// The live name a recorded cvar is applied under, or NULL for a retired one,
// which is matched first, ahead of an ini placeholder of its name.
static const char* replay_CvarName(const char* pszRecorded, bool bWarn, int value)
{
	for (unsigned int k = 0; k < countof(g_replayRetired); ++k)
	{
		if (stricmp(pszRecorded, g_replayRetired[k].name) != 0)
			continue;
		if (bWarn && (g_replayRetired[k].value != REPLAY_UNREAD)
			&& (value != g_replayRetired[k].value))
			Printf("fua_csnap_analyze: recorded cvar '%s' = %d is fixed at %d in this build\n",
				pszRecorded, value, g_replayRetired[k].value);
		return NULL;
	}
	for (unsigned int k = 0; k < countof(g_replayRenamed); ++k)
		if (stricmp(pszRecorded, g_replayRenamed[k].oldName) == 0)
			return g_replayRenamed[k].newName;
	return pszRecorded;
}

// A setting given on the command line, which holds over the recorded value
// for the whole replay, its segment changes included.
struct ReplayOverride
{
	FBaseCVar*		pVar;
	FString			name;
	int				value;
};

static const ReplayOverride* replay_FindOverride(const TArray<ReplayOverride>& ov,
	const FBaseCVar* pVar)
{
	for (unsigned int k = 0; k < ov.Size(); ++k)
		if (ov[k].pVar == pVar)
			return &ov[k];
	return NULL;
}

static void replay_ApplyCvars(const CsnapReader& r, const TArray<ReplayOverride>& ov,
	bool bAnnounce)
{
	for (unsigned int i = 0; i < r.cvarCount; ++i)
	{
		const char* pszName = replay_CvarName(r.cvars[i].name, true, r.cvars[i].value);
		if (pszName == NULL)
			continue;
		FBaseCVar* pVar = FindCVar(pszName, NULL);
		if ((pVar == NULL) || (pVar->GetFlags() & CVAR_AUTO))
		{
			// A recorded cvar this build lacks is reported, not ignored.
			Printf("fua_csnap_analyze: recorded cvar '%s' does not exist in this build\n",
				r.cvars[i].name);
			continue;
		}
		const ReplayOverride* pOv = replay_FindOverride(ov, pVar);
		UCVarValue v;
		v.Int = (pOv != NULL) ? pOv->value : r.cvars[i].value;
		pVar->SetGenericRep(v, CVAR_Int);
		if (bAnnounce && (pOv != NULL))
			Printf("  %s = %d (recorded %d, overridden)\n", pszName, pOv->value,
				r.cvars[i].value);
		else if (bAnnounce)
			Printf("  %s = %d\n", pszName, r.cvars[i].value);
	}

	// Overrides of settings the recording does not carry.
	for (unsigned int k = 0; k < ov.Size(); ++k)
	{
		bool bRecorded = false;
		for (unsigned int i = 0; (i < r.cvarCount) && (bRecorded == false); ++i)
		{
			const char* pszName = replay_CvarName(r.cvars[i].name, false, 0);
			bRecorded = (pszName != NULL) && (FindCVar(pszName, NULL) == ov[k].pVar);
		}
		if (bRecorded)
			continue;
		UCVarValue v;
		v.Int = ov[k].value;
		ov[k].pVar->SetGenericRep(v, CVAR_Int);
		if (bAnnounce)
			Printf("  %s = %d (overridden, not recorded)\n", ov[k].name.GetChars(),
				ov[k].value);
	}
}

// The value of one recorded cvar, by name, as the block's order means nothing.
static int clrec_SegCvar(const CsnapReader& r, const char* pszName)
{
	for (unsigned int i = 0; i < r.cvarCount; ++i)
	{
		const char* pszLive = replay_CvarName(r.cvars[i].name, false, 0);
		if ((pszLive != NULL) && (stricmp(pszLive, pszName) == 0))
			return r.cvars[i].value;
	}
	return -1;
}

// Keeps the ghost's newest position record if it is newer than the last kept.
static void replay_AccNote(ReplayEntity& e, unsigned int id)
{
	SnapGhostDebug d;
	if (GhostDebug(id, d) == false)
		return;
	if ((e.recCount > 0) && (d.posTic <= e.recTic[e.recCount - 1]))
		return;
	if (e.recCount == REPLAY_ACC_RECS)
	{
		for (unsigned int k = 1; k < REPLAY_ACC_RECS; ++k)
		{
			e.recTic[k - 1] = e.recTic[k];
			e.recX[k - 1] = e.recX[k]; e.recY[k - 1] = e.recY[k]; e.recZ[k - 1] = e.recZ[k];
			e.recVX[k - 1] = e.recVX[k]; e.recVY[k - 1] = e.recVY[k]; e.recVZ[k - 1] = e.recVZ[k];
		}
		--e.recCount;
	}
	e.recTic[e.recCount] = d.posTic;
	e.recX[e.recCount] = d.posX; e.recY[e.recCount] = d.posY; e.recZ[e.recCount] = d.posZ;
	e.recVX[e.recCount] = d.velX; e.recVY[e.recCount] = d.velY; e.recVZ[e.recCount] = d.velZ;
	++e.recCount;
}

// Scores a waiting frame against the records either side of its tic: 1 when
// scored, 0 while no record after it has arrived, -1 when it is older than
// the records kept.
static int replay_AccScore(ReplaySegment& seg, const ReplayEntity& e, const ReplayAccWait& w)
{
	if ((e.recCount == 0) || (w.tic > (double)e.recTic[e.recCount - 1]))
		return 0;
	if (w.tic < (double)e.recTic[0])
		return -1;
	unsigned int k = 0;
	while ((k + 1 < e.recCount) && ((double)e.recTic[k + 1] <= w.tic))
		++k;
	// [Kaori-Chani] The measurement works in doubles, and these are fixed_t: the raw is what
	// the rest of the function already reads them as, two lines down at (double)w.x.
	double tx = (double)e.recX[k], ty = (double)e.recY[k], tz = (double)e.recZ[k];
	bool bToRest = false;
	if ((k + 1 < e.recCount) && ((double)e.recTic[k] < w.tic))
	{
		bToRest = ((e.recVX[k + 1] | e.recVY[k + 1] | e.recVZ[k + 1]) == 0);
		// Through both records' velocities, as the sampler draws between them,
		// so a bridged arc is not scored against its chord.
		const double span = (double)(e.recTic[k + 1] - e.recTic[k]);
		const double f = (w.tic - e.recTic[k]) / span;
		tx = ComputeHermite(tx, (double)e.recVX[k], (double)e.recX[k + 1],
		(double)e.recVX[k + 1], span, f);
		ty = ComputeHermite(ty, (double)e.recVY[k], (double)e.recY[k + 1],
		(double)e.recVY[k + 1], span, f);
		tz = ComputeHermite(tz, (double)e.recVZ[k], (double)e.recZ[k + 1],
		(double)e.recVZ[k + 1], span, f);
	}
	const double dx = (double)w.x - tx, dy = (double)w.y - ty, dz = (double)w.z - tz;
	const double err = sqrt(dx * dx + dy * dy + dz * dz) / (double)FRACUNIT;
	if (bToRest)
	{
		++seg.accRestSteps;
		seg.accRestErrSum += err;
		return 1;
	}
	ComputeBinAdd(seg.accHist, err);
	++seg.accSteps;
	seg.accErrSum += err;
	if (err > seg.accErrMax)
		seg.accErrMax = err;
	if (err > 8.0)
		++seg.accOver8;
	if (err > 32.0)
		++seg.accOver32;
	return 1;
}

// Scores a waiting body frame as replay_AccScore does a ballistic one, but
// against the straight line between the records either side, since a body's
// velocity is what is under test.
static int replay_BodyAccScore(ReplaySegment& seg, const ReplayEntity& e,
	const ReplayAccWait& w)
{
	if ((e.recCount == 0) || (w.tic > (double)e.recTic[e.recCount - 1]))
		return 0;
	if (w.tic < (double)e.recTic[0])
		return -1;
	unsigned int k = 0;
	while ((k + 1 < e.recCount) && ((double)e.recTic[k + 1] <= w.tic))
		++k;
	// [Kaori-Chani] The measurement works in doubles, and these are fixed_t: the raw is what
	// the rest of the function already reads them as, two lines down at (double)w.x.
	double tx = (double)e.recX[k], ty = (double)e.recY[k], tz = (double)e.recZ[k];
	if ((k + 1 < e.recCount) && ((double)e.recTic[k] < w.tic))
	{
		const double f = (w.tic - e.recTic[k]) / (double)(e.recTic[k + 1] - e.recTic[k]);
		tx += ((double)e.recX[k + 1] - tx) * f;
		ty += ((double)e.recY[k + 1] - ty) * f;
		tz += ((double)e.recZ[k + 1] - tz) * f;
	}
	const double dx = (double)w.x - tx, dy = (double)w.y - ty, dz = (double)w.z - tz;
	const double err = sqrt(dx * dx + dy * dy + dz * dz) / (double)FRACUNIT;
	ComputeBinAdd(seg.bodyAccHist, err);
	++seg.bodyAccSteps;
	seg.bodyAccErrSum += err;
	if (err > seg.bodyAccErrMax)
		seg.bodyAccErrMax = err;
	if (err > 8.0)
		++seg.bodyAccOver8;
	if (err > 32.0)
		++seg.bodyAccOver32;
	return 1;
}

// Counts a waiting frame that will not be scored in its own line's unresolved.
static void replay_AccUnresolved(ReplaySegment& seg, const ReplayAccWait& w)
{
	if (w.bBody)
		++seg.bodyAccUnresolved;
	else
		++seg.accUnresolved;
}

// Drops every waiting frame unscored, at a segment's end or a reset.
static void replay_AccDrop(ReplaySegment& seg, TArray<ReplayAccWait>& waits)
{
	for (unsigned int i = 0; i < waits.Size(); ++i)
		replay_AccUnresolved(seg, waits[i]);
	waits.Clear();
}

// After a delta: scores the waiting frames the new records bracket, and drops
// those whose ghost left or was replaced, that fell behind the records kept, or
// that waited past REPLAY_ACC_WAIT_TICS.
static void replay_AccSweep(ReplaySegment& seg, TArray<ReplayEntity>& ents,
	TArray<ReplayAccWait>& waits)
{
	unsigned int kept = 0;
	for (unsigned int i = 0; i < waits.Size(); ++i)
	{
		const ReplayAccWait w = waits[i];
		ReplayEntity& e = ents[w.id];
		int r = -1;
		if ((w.serial == e.serial) && GhostPresent(w.id))
		{
			replay_AccNote(e, w.id);
			r = w.bBody ? replay_BodyAccScore(seg, e, w) : replay_AccScore(seg, e, w);
		}
		if ((r == 0) && ((double)NewestGameTic() - w.tic > REPLAY_ACC_WAIT_TICS))
			r = -1;
		if (r == 0)
			waits[kept++] = w;
		else if (r < 0)
			replay_AccUnresolved(seg, w);
	}
	waits.Resize(kept);
}

// The mirror's smoothing settings for one ghost, as the game applies them.
static SnapSmoothCfg replay_SmoothCfg(unsigned int id, int localPlayer)
{
	SnapSmoothCfg cfg;
	cfg.stepLimit = fua_csnap_smooth_step;
	cfg.extrapLimit = fua_csnap_smooth_nonballistic;
	cfg.missileLimit = fua_csnap_smooth_ballistic;
	cfg.bStepPop = ComputeSmoothStepPop(fua_csnap_smooth_entities, id, localPlayer, MAXPLAYERS);
	cfg.bInReset = false;
	cfg.ticRate = TICRATE;
	return cfg;
}

// Forgets a ghost's plan C smoothing with its occupant or segment, keeping what
// it already counted, so its next refill draws its new state outright.
static void replay_RefillForget(ReplayRefill& c)
{
	memset(&c.base, 0, sizeof(c.base));
	memset(&c.counted, 0, sizeof(c.counted));
	c.drawn = false;
}

// A plan C refill of one ghost at the frame's time, `nowMS`, folding a record
// once there and smoothing the tic's end on from it (cl_replay.md, Use).
static void replay_Refill(ReplayRefill& c, ReplayEntity& e, unsigned int id, bool bTicFirst,
	bool bBackdate, bool bAnchorAll, unsigned int startMS, unsigned int nowMS,
	unsigned int endMS, fixed_t frac16, int localPlayer, SnapSmoothStats& st)
{
	// Where this frame draws the ghost from its last refill, if that was in this
	// tic: the anchor when the frame itself cannot be sampled.
	const bool bWasDrawn = c.drawn && (bTicFirst == false);
	double hereX = (double)(c.sx + FixedMul(frac16, c.ex - c.sx));
	double hereY = (double)(c.sy + FixedMul(frac16, c.ey - c.sy));
	double hereZ = (double)(c.sz + FixedMul(frac16, c.ez - c.sz));

	if (bTicFirst)
	{
		c.base = e.smooth;
		memset(&c.counted, 0, sizeof(c.counted));
	}
	const SnapSmooth atStart = c.base;
	c.drawn = false;
	c.noStart = false;
	c.backdated = false;
	c.anchored = false;
	c.snapped = false;
	c.ok = SampleGhost(id, endMS, c.end);
	c.endExit = SampleExit();
	if (c.ok == false)
		return;
	const SnapSmoothCfg cfg = replay_SmoothCfg(id, localPlayer);
	SnapSmoothStats fold, drawnPart;

	// The fold at the frame, kept within the tic, as the default path draws.
	SnapGhostPose here;
	const bool bHere = SampleGhost(id, nowMS, here);
	c.hereExit = bHere ? SampleExit() : NULL;
	if (bHere)
	{
		const bool bWasValid = c.base.valid;
		SnapSmoothStats atFrame;
		memset(&atFrame, 0, sizeof(atFrame));
		fixed_t hx, hy, hz;
		ComputeSmooth(c.base, ComputeSmoothIn(here), cfg, nowMS, atFrame, hx, hy, hz);
		hereX = (double)hx; hereY = (double)hy; hereZ = (double)hz;
		c.snapped = (bWasValid == false) || ((atFrame.snaps[0] | atFrame.snaps[1]) != 0);
		ComputeCensusSplit(atFrame, fold, drawnPart);
		ComputeCensusAdd(st, fold, false);
	}

	// The tic's end, on from the frame's state; its drawn counts are the tic's.
	SnapSmooth carried = c.base;
	SnapSmoothStats atEnd;
	memset(&atEnd, 0, sizeof(atEnd));
	ComputeSmooth(carried, ComputeSmoothIn(c.end), cfg, endMS, atEnd, c.ex, c.ey, c.ez);
	e.smooth = carried;
	c.eMinusRaw[0] = c.ex - c.end.x;
	c.eMinusRaw[1] = c.ey - c.end.y;
	c.eMinusRaw[2] = c.ez - c.end.z;
	c.hMinusE[0] = (fixed_t)(hereX - (double)c.ex);
	c.hMinusE[1] = (fixed_t)(hereY - (double)c.ey);
	c.hMinusE[2] = (fixed_t)(hereZ - (double)c.ez);
	ComputeCensusSplit(atEnd, fold, drawnPart);
	ComputeCensusAdd(st, fold, false);
	ComputeCensusAdd(st, c.counted, true);
	ComputeCensusAdd(st, drawnPart, false);
	c.counted = drawnPart;

	c.drawn = true;
	c.startExit = NULL;

	// Mid-tic, or always under anchor, the start is set so this frame draws the
	// fold's output, the end drawn outright in a tic's last sixteenth.
	if (bWasDrawn || (bAnchorAll && bHere))
	{
		// As an offset from the end, rounded, so a fold that lands on the end
		// gives the end exactly.
		const double f = (double)frac16 / (double)FRACUNIT;
		c.sx = c.ex; c.sy = c.ey; c.sz = c.ez;
		if (1.0 - f >= 1.0 / 16.0)
		{
			c.sx += (fixed_t)floor((hereX - (double)c.ex) / (1.0 - f) + 0.5);
			c.sy += (fixed_t)floor((hereY - (double)c.ey) / (1.0 - f) + 0.5);
			c.sz += (fixed_t)floor((hereZ - (double)c.ez) / (1.0 - f) + 0.5);
			c.anchored = true;
		}
		c.sMinusE[0] = c.sx - c.ex; c.sMinusE[1] = c.sy - c.ey; c.sMinusE[2] = c.sz - c.ez;
		return;
	}
	c.snapped = false;
	SnapGhostPose start;
	const bool bStart = SampleGhost(id, startMS, start);
	c.startExit = bStart ? SampleExit() : NULL;
	if (bStart)
	{
		SnapSmooth copy = atStart;
		SnapSmoothStats scratch;
		memset(&scratch, 0, sizeof(scratch));
		ComputeSmooth(copy, ComputeSmoothIn(start), cfg, startMS, scratch, c.sx, c.sy, c.sz);
		c.sMinusE[0] = c.sx - c.ex; c.sMinusE[1] = c.sy - c.ey; c.sMinusE[2] = c.sz - c.ez;
		return;
	}

	// A ghost not drawable at the tic's start starts at its end, or a span of
	// its velocity behind under backdate.
	const double tics = (double)(endMS - startMS) * TICRATE / 1000.0;
	c.noStart = true;
	c.backdated = bBackdate && ((c.end.vx | c.end.vy | c.end.vz) != 0);
	c.sx = c.ex; c.sy = c.ey; c.sz = c.ez;
	if (c.backdated)
	{
		c.sx -= (fixed_t)((double)c.end.vx * tics);
		c.sy -= (fixed_t)((double)c.end.vy * tics);
		c.sz -= (fixed_t)((double)c.end.vz * tics);
	}
	c.sMinusE[0] = c.sx - c.ex; c.sMinusE[1] = c.sy - c.ey; c.sMinusE[2] = c.sz - c.ez;
}

// Summarises one segment and writes its CSV rows, at each boundary, so the
// accumulators reset and an interrupted replay still reports.
static void replay_FlushSegment(const ReplaySegment& seg, TArray<ReplayEntity>& ents,
	TArray<ReplayClassRate>& classes, FILE* pCsv, bool bRefill)
{
	unsigned int act = 0, jumps = 0, worstJump = 0;
	unsigned int wSrv = 0, oSrv = 0, wStv = 0, oStv = 0, glideE = 0;
	unsigned int worstSrv = 0, worstStv = 0, worstSrvId = 0, worstStvId = 0;
	QWORD delayAcc = 0, drawnAcc = 0;

	for (unsigned int id = 0; id < SNAP_MAXID; ++id)
	{
		ReplayEntity& e = ents[id];
		if (e.drawn == 0)
			continue;

		// A settled decoration has a real age of tens of seconds and says
		// nothing about the sampler; including it swamps every mean.
		const double meanDelay = (double)e.delaySumMS / (double)e.drawn;
		const bool bActive = (meanDelay < 1000.0);

		if (pCsv != NULL)
		{
			fprintf(pCsv, "%u,%u,%u,%u,%u,%u,%.1f,%u,%u,%u,%u,%u,%u,%u,%u,%d\n",
				seg.index, id, e.samples, e.drawn, e.jumps, e.worstJump,
				meanDelay, e.delayMaxMS, e.winServed, e.overServed,
				e.winStarved, e.overStarved, e.glideEvents,
				e.worstServedPct, e.worstStarvedPct, bActive ? 1 : 0);
		}

		if (bActive == false)
			continue;

		++act;
		jumps += e.jumps;
		wSrv += e.winServed;  oSrv += e.overServed;
		wStv += e.winStarved; oStv += e.overStarved;
		glideE += e.glideEvents;
		delayAcc += e.delaySumMS;
		drawnAcc += e.drawn;
		if (e.worstJump > worstJump) worstJump = e.worstJump;
		if (e.worstServedPct > worstSrv)  { worstSrv = e.worstServedPct;  worstSrvId = id; }
		if (e.worstStarvedPct > worstStv) { worstStv = e.worstStarvedPct; worstStvId = id; }
	}

	const double f = (seg.frames > 0) ? (double)seg.frames : 1.0;
	Printf("[seg %u] %.1f s, %u frames\n",
		seg.index, (double)(seg.endMS - seg.startMS) / 1000.0, seg.frames);
	Printf("         %u active entities | mean delay %.0f ms | snaps %.3f/frame "
		"(worst %u u)\n", act,
		(drawnAcc > 0) ? ((double)(QWORD)delayAcc / (double)(QWORD)drawnAcc) : 0.0,
		(double)jumps / f, worstJump);
	// A recording often ends on a reset, so its range says more than its state.
	SnapClockStats ck;
	ClockStats(ck, true);
	char szClock[384];
	ClockDescribe(ck, szClock, sizeof(szClock));
	Printf("         clock %s\n", szClock);
	if (bRefill)
	{
		const unsigned int refills = seg.refillsTic + seg.refillsDelta;
		Printf("         plan C: %u refills, %u on a new tic and %u on a delta alone,"
			" %.2f frames each | %u ghost refills with no start pose, %u backdated | %u ghost"
			" refills anchored, %u of them after a snap or a fresh start\n",
			refills, seg.refillsTic, seg.refillsDelta,
			(refills > 0) ? (double)seg.frames / (double)refills : 0.0,
			seg.refillsNoStart, seg.refillsBackdated, seg.refillsAnchored, seg.refillsSnapped);
	}
	if (seg.balSteps > 0)
	{
		Printf("         ballistic draw error %.2f u mean over %u steps, p99 %.1f,"
			" max %.1f | %u past 8 u, %u past 32 u\n", seg.balErrSum / seg.balSteps,
			seg.balSteps, ComputeBinP99(seg.balHist, seg.balSteps), seg.balErrMax,
			seg.balOver8, seg.balOver32);
		const PClass* pW = NETWORK_GetClassFromIdentification(seg.balWorstClass);
		Printf("         worst step %.1f u: id %u (%s) at %u ms, exit %s", seg.balErrMax,
			seg.balWorstId, (pW != NULL) ? pW->TypeName.GetChars() : "?", seg.balWorstMS,
			(seg.balWorstExit != NULL) ? seg.balWorstExit : "?");
		if (bRefill)
			Printf(", start %s, %u frame%s after a %s refill\n",
				seg.balWorstAnchored
					? (seg.balWorstSnapped ? "anchored after a snap or a fresh start" : "anchored")
					: (seg.balWorstStart != NULL) ? seg.balWorstStart
					: (seg.balWorstBackdated ? "not drawable, backdated" : "not drawable"),
				seg.balWorstSince, (seg.balWorstSince == 1) ? "" : "s",
				(seg.balWorstRefill == 't') ? "new-tic" : "delta");
		else
			Printf("\n");
	}
	if ((seg.accSteps | seg.accRestSteps | seg.accUnresolved) != 0)
	{
		Printf("         ballistic accuracy %.2f u mean over %u frames, p99 %.1f, max %.1f"
			" | %u past 8 u, %u past 32 u | %.2f u over %u before a stop | %u unresolved\n",
			(seg.accSteps > 0) ? (seg.accErrSum / seg.accSteps) : 0.0, seg.accSteps,
			ComputeBinP99(seg.accHist, seg.accSteps), seg.accErrMax, seg.accOver8,
			seg.accOver32, (seg.accRestSteps > 0) ? (seg.accRestErrSum / seg.accRestSteps) : 0.0,
			seg.accRestSteps, seg.accUnresolved);
	}
	if ((bRefill == false) && ((seg.bodyAccSteps | seg.bodyAccUnresolved) != 0))
	{
		Printf("         body accuracy %.2f u mean over %u frames, p99 %.1f, max %.1f | %u past"
			" 8 u, %u past 32 u | %u unresolved\n",
			(seg.bodyAccSteps > 0) ? (seg.bodyAccErrSum / seg.bodyAccSteps) : 0.0,
			seg.bodyAccSteps, ComputeBinP99(seg.bodyAccHist, seg.bodyAccSteps),
			seg.bodyAccErrMax, seg.bodyAccOver8, seg.bodyAccOver32, seg.bodyAccUnresolved);
	}
	SnapMissileCensus mc;
	MissileCensus(mc, true);
	if ((mc.aheadInterp | mc.aheadFirst | mc.aheadJump) != 0)
	{
		Printf("         dated ahead: %u between records, held %u with none earlier,"
			" %u at a jump%s\n", mc.aheadInterp, mc.aheadFirst, mc.aheadJump,
			bRefill ? " (counted at each of a refill's samples)" : "");
	}
	const SnapSmoothStats& sm = seg.smooth;
	if ((sm.folds[0] | sm.folds[1] | sm.snaps[0] | sm.snaps[1] | sm.zero | sm.movingFrames) != 0)
	{
		// Under plan C the census counts each client tic once, from its latest refill.
		const char* pszUnit = bRefill ? "tics" : "frames";
		Printf("         smoothing: stepped %u folds (mean %.1f u), %u snaps | extrapolated"
			" %u folds (mean %.2f u), %u snaps, residual <0.5 %u <2 %u <8 %u 8+ %u | %u %s"
			" off target, %u stepped OUTSIDE its records | REVERSED %u of %u moving %s\n",
			sm.folds[0], (sm.folds[0] > 0) ? sm.foldSum[0] / sm.folds[0] : 0.0, sm.snaps[0],
			sm.folds[1], (sm.folds[1] > 0) ? sm.foldSum[1] / sm.folds[1] : 0.0, sm.snaps[1],
			sm.resid[1][0], sm.resid[1][1], sm.resid[1][2], sm.resid[1][3], sm.offFrames,
			pszUnit, sm.past, sm.reversed, sm.movingFrames, pszUnit);
		// Ballistic, turning, then bodies, model-dated first.
		static const unsigned int order[SNAP_SMOOTH_KINDS] =
			{ SNAP_SMOOTH_BALLISTIC, SNAP_SMOOTH_TURNING, SNAP_SMOOTH_BODY };
		const double secs = (double)(seg.endMS - seg.startMS) / 1000.0;
		Printf("         smoothing by kind, %.1f frames/s:\n",
			(secs > 0.0) ? (double)seg.frames / secs : 0.0);
		char szCell[320];
		for (unsigned int i = 0; i < SNAP_SMOOTH_KINDS * 2; ++i)
		{
			if (ComputeSmoothCellText(sm, order[i / 2], 1 - (i & 1), pszUnit, szCell,
				sizeof(szCell)))
				Printf("           %s\n", szCell);
		}
	}

	// The glide census, a tripwire whose rise in SERVED on one recording flags
	// a sampler change.
	Printf("         glide (tripwire) SERVED  %.2f%% of %u windows | %u sustained | worst %u%% (id %u)\n",
		(wSrv > 0) ? (100.0 * (double)oSrv / (double)wSrv) : 0.0, wSrv, glideE,
		worstSrv, worstSrvId);
	Printf("         glide STARVED %.2f%% of %u windows | worst %u%% (id %u)"
		"   <- refresh rate, not the sampler\n",
		(wStv > 0) ? (100.0 * (double)oStv / (double)wStv) : 0.0, wStv,
		worstStv, worstStvId);

	// Observed rate by class against its bound, the most-sampled first.
	{
		unsigned int picked[CLASSRATE_ROWS];
		unsigned int shown = 0;
		while (shown < CLASSRATE_ROWS)
		{
			unsigned int best = CLASSRATE_MAX;
			for (unsigned int c = 0; c < CLASSRATE_MAX; ++c)
			{
				if (classes[c].wins < CLASSRATE_MINWINS)
					continue;
				bool bPicked = false;
				for (unsigned int i = 0; i < shown; ++i)
					bPicked = bPicked || (picked[i] == c);
				if (bPicked)
					continue;
				if ((best == CLASSRATE_MAX) || (classes[c].wins > classes[best].wins))
					best = c;
			}
			if (best == CLASSRATE_MAX)
				break;
			picked[shown] = best;
			const ReplayClassRate& k = classes[best];
			const PClass* pT = NETWORK_GetClassFromIdentification((unsigned short)best);
			Printf("           %-20s %6u win  mean %4u%% peak %5u%% of Speed  bound %u%%\n",
				(pT != NULL) ? pT->TypeName.GetChars() : "?", k.wins,
				(unsigned int)(k.pctSum / k.wins), k.pctPeak,
				k.bMissile ? (unsigned int)fua_csnap_glide_missile
					: (unsigned int)fua_csnap_glide_monster);
			++shown;
		}
	}

	// The worst windows by class, as a figure means one thing per class.
	{
		// The class the worst window itself recorded: the ghost has usually
		// left the table by the flush. 0 is no over-bound window.
		const unsigned short srvClass = ents[worstSrvId].worstServedClass;
		const unsigned short stvClass = ents[worstStvId].worstStarvedClass;
		const PClass* pS = (srvClass != 0)
			? NETWORK_GetClassFromIdentification(srvClass) : NULL;
		const PClass* pT = (stvClass != 0)
			? NETWORK_GetClassFromIdentification(stvClass) : NULL;
		Printf("         worst classes: served %s, starved %s\n",
			(pS != NULL) ? pS->TypeName.GetChars() : "?",
			(pT != NULL) ? pT->TypeName.GetChars() : "?");
	}

	memset(&ents[0], 0, SNAP_MAXID * sizeof(ReplayEntity));
	memset(&classes[0], 0, CLASSRATE_MAX * sizeof(ReplayClassRate));
}

// The replay's animates answer from the recorded fua_csnap_localanim, exact at
// 0 and 3, as a replay has no tiers.
static bool g_replayAnimates = true;

static bool replay_Animates(unsigned int)
{
	return g_replayAnimates;
}

static void replay_SetAnimates(const CsnapReader& r, const TArray<ReplayOverride>& ov)
{
	const ReplayOverride* pOv = replay_FindOverride(ov, FindCVar("fua_csnap_localanim", NULL));
	const int level = (pOv != NULL) ? pOv->value : clrec_SegCvar(r, "fua_csnap_localanim");
	g_replayAnimates = (level != 0);
	if ((level == 1) || (level == 2))
		Printf("fua_csnap_analyze: fua_csnap_localanim %d splits by tier, which a replay "
			"cannot reproduce; every ghost animates\n", level);
	else if (level < 0)
		Printf("fua_csnap_analyze: fua_csnap_localanim not recorded; every ghost animates\n");
}

CCMD(fua_csnap_analyze)
{
	if (argv.argc() < 2)
	{
		Printf("usage: fua_csnap_analyze <file> [<id> | csv <out>] [refill | backdate | anchor]"
			" [<setting>=<value> ...]\n");
		return;
	}

	// The replay clears the ghost table, which a connection or a demo is using:
	// the server would go on sending deltas against ghosts the client dropped.
	if (NETWORK_InClientMode())
	{
		Printf("fua_csnap_analyze: disconnect or stop the demo first; a replay clears "
			"the ghost table\n");
		return;
	}

	// name=value arguments are overrides, whole-number or on/off settings only.
	TArray<ReplayOverride> overrides;
	const char* pszPos[2] = { NULL, NULL };
	unsigned int nPos = 0;
	bool bRefill = false, bBackdate = false, bAnchorAll = false;
	for (int i = 2; i < argv.argc(); ++i)
	{
		const char* pszEq = strchr(argv[i], '=');

		// The mode keywords count anywhere but as the CSV's file name.
		const bool bKeywordPlace = (pszEq == NULL)
			&& ((nPos != 1) || (stricmp(pszPos[0], "csv") != 0));
		if (bKeywordPlace && (stricmp(argv[i], "refill") == 0))
		{
			bRefill = true;
			continue;
		}
		if (bKeywordPlace && (stricmp(argv[i], "backdate") == 0))
		{
			bRefill = true;
			bBackdate = true;
			continue;
		}
		if (bKeywordPlace && (stricmp(argv[i], "anchor") == 0))
		{
			bRefill = true;
			bAnchorAll = true;
			continue;
		}
		if (pszEq == NULL)
		{
			if (nPos == 2)
			{
				Printf("fua_csnap_analyze: '%s' is one argument too many\n", argv[i]);
				return;
			}
			pszPos[nPos++] = argv[i];
			continue;
		}
		const FString name(argv[i], (size_t)(pszEq - argv[i]));
		FBaseCVar* pVar = FindCVar(name.GetChars(), NULL);
		char* pszEnd = NULL;
		const long value = strtol(pszEq + 1, &pszEnd, 10);
		if ((pVar == NULL) || (pVar->GetFlags() & CVAR_AUTO) || (pszEq[1] == '\0')
			|| (*pszEnd != '\0')
			|| ((pVar->GetRealType() != CVAR_Int) && (pVar->GetRealType() != CVAR_Bool)))
		{
			Printf("fua_csnap_analyze: '%s' is not <setting>=<number> for a setting of this "
				"build\n", argv[i]);
			return;
		}
		ReplayOverride o;
		o.pVar = pVar;
		o.name = name;
		o.value = (int)value;
		overrides.Push(o);
	}
	const bool bCsv = (nPos >= 1) && (stricmp(pszPos[0], "csv") == 0);
	if (bCsv && (nPos < 2))
	{
		Printf("fua_csnap_analyze: csv needs an output file\n");
		return;
	}
	if ((nPos == 2) && (bCsv == false))
	{
		Printf("fua_csnap_analyze: '%s' is one argument too many\n", pszPos[1]);
		return;
	}
	const char* pszCsvPath = bCsv ? pszPos[1] : NULL;
	int traceId = -1;
	if ((nPos >= 1) && (bCsv == false))
	{
		char* pszIdEnd = NULL;
		const long id = strtol(pszPos[0], &pszIdEnd, 10);
		if ((*pszIdEnd != '\0') || (id < 1) || (id > 65535))
		{
			Printf("fua_csnap_analyze: '%s' is not an entity id (1-65535)\n", pszPos[0]);
			return;
		}
		traceId = (int)id;
	}

	CsnapReader r;
	if (CsnapOpen(r, argv[1]) == false)
	{
		Printf("fua_csnap_analyze: %s: %s\n", argv[1], (r.error != NULL) ? r.error : "failed");
		CsnapClose(r);
		return;
	}

	Printf("fua_csnap_analyze: %s (v%u, %u Hz, %u cvars recorded, player %d)\n",
		argv[1], r.version, r.ticrate, r.cvarCount, r.localPlayer);
	if (bRefill)
		Printf("fua_csnap_analyze: plan C: poses at each client tic's start and end,"
			" refilled on a new tic or a delta, drawn blended by the fraction of the"
			" span sampled; tics are phased from the first frame%s\n",
			bAnchorAll ? "; every refill anchors its start to the fold at its frame"
				: bBackdate ? "; a ghost entering mid-tic is backdated by its velocity" : "");

	// The recording's own settings, applied before any sampling and restored.
	TArray<int> savedValues;
	TArray<FString> savedNames;
	for (unsigned int i = 0; i < r.cvarCount; ++i)
	{
		const char* pszName = replay_CvarName(r.cvars[i].name, false, 0);
		FBaseCVar* pVar = (pszName != NULL) ? FindCVar(pszName, NULL) : NULL;
		if ((pVar != NULL) && ((pVar->GetFlags() & CVAR_AUTO) == 0))
		{
			savedNames.Push(FString(pszName));
			savedValues.Push(pVar->GetGenericRep(CVAR_Int).Int);
		}
	}
	for (unsigned int k = 0; k < overrides.Size(); ++k)
	{
		bool bSaved = false;
		for (unsigned int i = 0; (i < savedNames.Size()) && (bSaved == false); ++i)
			bSaved = (FindCVar(savedNames[i].GetChars(), NULL) == overrides[k].pVar);
		if (bSaved == false)
		{
			savedNames.Push(overrides[k].name);
			savedValues.Push(overrides[k].pVar->GetGenericRep(CVAR_Int).Int);
		}
	}
	if (overrides.Size() > 0)
		Printf("fua_csnap_analyze: applying recorded settings, then %u override%s\n",
			overrides.Size(), (overrides.Size() == 1) ? "" : "s");
	else
		Printf("fua_csnap_analyze: applying recorded settings\n");
	replay_ApplyCvars(r, overrides, true);

	// The replay owns the ghost table for its duration, and the map's frame,
	// which is narrow at origin 0 until a CSNAP_MAP says otherwise.
	ClientReset();
	GhostClearAll();
	SetStreamVersion(ZSNAP_VERSION);
	// The clock's report counters outlive a reset, so whatever ran before the
	// replay is drained out of its first segment.
	SnapClockStats drained;
	ClockStats(drained, true);
	const SnapFrame savedFrame = ClientMapFrame();
	UseMapFrame(FrameNarrow());

	// Its own hooks for the run, no sinks and no law until a matching map, the
	// session's restored at the end.
	const ClentLawGravityFn savedLaw = SetLawGravity(NULL);
	const ClentAnimatesFn savedAnimates = SetAnimatesLookup(replay_Animates);
	const ClentMoverFn savedMover = SetMoverSink(NULL);
	const ClentLocalFn savedLocal = SetLocalSink(NULL);
	const ClentPSpriteFn savedPSprite = SetPSpriteSink(NULL);
	replay_SetAnimates(r, overrides);

	BYTE levelChecksum[16];
	const bool bLevel = RecorderLevelChecksum(levelChecksum);
	bool bMapNamed = false, bNoMapSaid = false;

	// The recording's player, which the next drawn frame puts back.
	SetLocalPlayerId((r.localPlayer >= 0)
		? (unsigned int)(r.localPlayer + 1) : 0u);

	TArray<ReplayEntity> ents;
	ents.Resize(SNAP_MAXID);
	memset(&ents[0], 0, SNAP_MAXID * sizeof(ReplayEntity));
	TArray<ReplayAccWait> waits;

	// Plan C's per-ghost poses, and when the next frame must refill them.
	TArray<ReplayRefill> refill;
	if (bRefill)
	{
		refill.Resize(SNAP_MAXID);
		memset(&refill[0], 0, SNAP_MAXID * sizeof(ReplayRefill));
	}
	const double tickMS = 1000.0 / (double)TICRATE;
	double ticPhaseMS = -1.0;
	long long refillTic = LLONG_MIN;
	bool bRefillDue = true;
	unsigned int framesSinceRefill = 0;
	char lastRefillKind = 't';

	// The live session's dated-ahead counts are not the recording's.
	{
		SnapMissileCensus mc;
		MissileCensus(mc, true);
	}
	TArray<ReplayClassRate> classes;
	classes.Resize(CLASSRATE_MAX);
	memset(&classes[0], 0, CLASSRATE_MAX * sizeof(ReplayClassRate));
	TArray<BYTE> classSeen;
	classSeen.Resize(SNAP_CLASS_MASK + 1);
	memset(&classSeen[0], 0, SNAP_CLASS_MASK + 1);

	FILE* pCsv = NULL;
	if (bCsv)
	{
		pCsv = fopen(pszCsvPath, "wb");
		if (pCsv == NULL)
			Printf("fua_csnap_analyze: cannot write %s\n", pszCsvPath);
		else
			fprintf(pCsv, "segment,id,samples,drawn,jumps,worstJumpUnits,"
				"meanDelayMS,maxDelayMS,winServed,overServed,winStarved,overStarved,"
				"glideEvents,worstServedPct,worstStarvedPct,active\n");
	}

	// Segment boundaries are cvar changes, so a recording can alternate a
	// setting freely rather than needing one switch and a hand-split file.
	ReplaySegment seg;
	memset(&seg, 0, sizeof(seg));
	seg.index = 0;

	unsigned int deltas = 0, frames = 0, resets = 0, applyErrors = 0, cvarSwitches = 0;
	unsigned int drawnRecs = 0, otherRecs = 0;
	// Summed as compared, as replay_FlushSegment clears `ents`.
	QWORD totalCmp = 0, totalAgree = 0;
	unsigned int firstMS = 0, lastMS = 0;

	for (;;)
	{
		const ESnapReadResult res = CsnapReadRecord(r);
		if (res == SNAPREAD_END)
			break;
		if (res == SNAPREAD_TRUNCATED)
		{
			Printf("fua_csnap_analyze: truncated after %u records (expected of a crash tail)\n",
				r.recordIndex - 1);
			break;
		}
		if (res == SNAPREAD_ERROR)
		{
			Printf("fua_csnap_analyze: error at record %u: %s\n", r.recordIndex,
				(r.error != NULL) ? r.error : "unknown");
			break;
		}

		if (firstMS == 0)
			firstMS = r.timeMS;
		lastMS = r.timeMS;

		if (r.cvarsChanged)
		{
			++cvarSwitches;

			// Close the span that just ended before applying anything, so its
			// numbers describe only the settings that were in force for it.
			seg.endMS = r.timeMS;
			replay_AccDrop(seg, waits);
			if (seg.frames > 0)
				replay_FlushSegment(seg, ents, classes, pCsv, bRefill);

			replay_ApplyCvars(r, overrides, false);
			replay_SetAnimates(r, overrides);
			bRefillDue = true;

			// The flush starts every ghost's smoothing over, so under refill its
			// tic-start copy goes too, and its census belongs to the old segment.
			for (unsigned int id = 0; id < refill.Size(); ++id)
				replay_RefillForget(refill[id]);

			memset(&seg, 0, sizeof(seg));
			seg.index = cvarSwitches;
			seg.startMS = r.timeMS;
		}

		if (r.kind == CSNAP_RESET)
		{
			ClientReset();
			GhostClearAll();
			for (unsigned int id = 0; id < SNAP_MAXID; ++id)
			{
				ents[id].balHave = false;
				ents[id].recCount = 0;
				memset(&ents[id].smooth, 0, sizeof(ents[id].smooth));
				if (bRefill)
					replay_RefillForget(refill[id]);
			}
			replay_AccDrop(seg, waits);
			SetLawGravity(NULL);
			bMapNamed = false;
			bRefillDue = true;
			++resets;
			continue;
		}

		// Per map: the law is drawn against the loaded level, so it runs only
		// where that is the recording's.
		if (r.kind == CSNAP_MAP)
		{
			const bool bMatch = bLevel
				&& (memcmp(r.mapChecksum, levelChecksum, sizeof(levelChecksum)) == 0);
			SetLawGravity(bMatch ? LawAt : NULL);
			UseMapFrame(r.mapFrame);
			bRefillDue = true;
			if (bMatch == false)
				Printf("fua_csnap_analyze: recorded on %s, but %s; the gravity law is "
					"off for it\n", r.mapName,
					bLevel ? "another map is loaded" : "no level is loaded");
			bMapNamed = true;
			continue;
		}

		// Explicit, and BEFORE the frame path: that path is the fall-through,
		// and each frame sweeps every id.
		if (r.kind == CSNAP_MIRROR)
		{
			if (r.mirrorId < SNAP_MAXID)
			{
				ReplayEntity& e = ents[r.mirrorId];
				e.lastSprite = r.mirrorSprite;
				e.lastFrame = r.mirrorFrame;
				e.haveDrawn = true;
			}
			++drawnRecs;
			continue;
		}

		if (r.kind == CSNAP_DELTA)
		{
			if ((bMapNamed == false) && (bNoMapSaid == false))
			{
				Printf("fua_csnap_analyze: deltas with no map named; the gravity law is "
					"off until one is\n");
				bNoMapSaid = true;
			}
			char err[192];
			if (ApplyDelta(r.payload, r.payloadSize, r.serverTic, r.gameTic,
				r.timeMS, err, sizeof(err), NULL) != 0)
			{
				if (applyErrors == 0)
					Printf("fua_csnap_analyze: first decode error: %s\n", err);
				++applyErrors;
			}
			++deltas;
			bRefillDue = true;

			// A new occupant's records and smoothing are not the old one's.
			unsigned int nEntered = 0;
			const unsigned int* pEntered = EnteredIds(nEntered);
			for (unsigned int i = 0; i < nEntered; ++i)
			{
				ents[pEntered[i]].recCount = 0;
				++ents[pEntered[i]].serial;
				memset(&ents[pEntered[i]].smooth, 0, sizeof(ents[pEntered[i]].smooth));
				if (bRefill)
					replay_RefillForget(refill[pEntered[i]]);
			}
			if (waits.Size() > 0)
				replay_AccSweep(seg, ents, waits);
			continue;
		}

		// Any other kind is skipped, never sampled as a frame.
		if (r.kind != CSNAP_FRAME)
		{
			++otherRecs;
			continue;
		}

		++frames;
		++seg.frames;
		if (seg.startMS == 0)
			seg.startMS = r.timeMS;
		seg.endMS = r.timeMS;

		// Plan C: the client tic this frame falls in, and whether its poses
		// must be refilled first.
		bool bRefillNow = false, bTicFirst = false;
		unsigned int ticStartMS = 0, ticEndMS = 0;
		fixed_t frac16 = FRACUNIT;
		if (bRefill)
		{
			if (ticPhaseMS < 0.0)
				ticPhaseMS = (double)r.timeMS;
			const long long tic = (long long)floor(((double)r.timeMS - ticPhaseMS) / tickMS);
			const double startExact = ticPhaseMS + (double)tic * tickMS;
			ticStartMS = (unsigned int)(startExact + 0.5);
			ticEndMS = (unsigned int)(startExact + tickMS + 0.5);

			// Over the span actually sampled, whole milliseconds at both edges, so
			// a pose moves at its own speed within the tic.
			frac16 = (fixed_t)(((double)(r.timeMS - ticStartMS)
				/ (double)(ticEndMS - ticStartMS)) * (double)FRACUNIT);
			if (tic != refillTic)
			{
				++seg.refillsTic;
				bRefillNow = true;
				bTicFirst = true;
				lastRefillKind = 't';
			}
			else if (bRefillDue)
			{
				++seg.refillsDelta;
				bRefillNow = true;
				lastRefillKind = 'd';
			}
			framesSinceRefill = bRefillNow ? 0 : (framesSinceRefill + 1);
			refillTic = tic;
			bRefillDue = false;
		}

		for (unsigned int id = 0; id < SNAP_MAXID; ++id)
		{
			if (GhostPresent(id) == false)
				continue;

			ReplayEntity& e = ents[id];
			++e.samples;
			classSeen[GhostClassId(id) & SNAP_CLASS_MASK] = 1;

			// What the mirror draws, the pose through its smoothing, bar the
			// clamps a replay has no mirror for.
			SnapGhostPose pose;
			const char* pszExit = NULL;
			if (bRefill)
			{
				ReplayRefill& c = refill[id];
				if (bRefillNow)
				{
					replay_Refill(c, e, id, bTicFirst, bBackdate, bAnchorAll, ticStartMS, r.timeMS,
						ticEndMS, frac16, r.localPlayer, seg.smooth);
					if (c.ok && c.noStart)
						++seg.refillsNoStart;
					if (c.ok && c.backdated)
						++seg.refillsBackdated;
					if (c.ok && c.anchored)
						++seg.refillsAnchored;
					if (c.ok && c.anchored && c.snapped)
						++seg.refillsSnapped;
				}
				pszExit = c.endExit;
				if (c.ok == false)
				{
					e.balHave = false;
					continue;			// not drawable yet: presence-at-entry
				}

				// The end pose, its position blended from the start as the
				// render copy does, and its ages taken at the frame.
				pose = c.end;
				pose.x = c.sx + FixedMul(frac16, c.ex - c.sx);
				pose.y = c.sy + FixedMul(frac16, c.ey - c.sy);
				pose.z = c.sz + FixedMul(frac16, c.ez - c.sz);
				const unsigned int ahead = ticEndMS - r.timeMS;
				pose.arrivalAgeMS = (pose.arrivalAgeMS > ahead) ? (pose.arrivalAgeMS - ahead) : 0;
				pose.ageMS = (pose.ageMS > ahead) ? (pose.ageMS - ahead) : 0;
			}
			else
			{
				if (SampleGhost(id, r.timeMS, pose) == false)
				{
					e.balHave = false;
					continue;			// not drawable yet: presence-at-entry
				}
				pszExit = SampleExit();
				ComputeSmooth(e.smooth, ComputeSmoothIn(pose), replay_SmoothCfg(id, r.localPlayer),
					r.timeMS, seg.smooth, pose.x, pose.y, pose.z);

				// Body accuracy: a body with a wire velocity, held at its records.
				double bodyTic;
				if ((id >= MAXPLAYERS) && (MissileFlagged(pose.misc) == false)
					&& ((pose.vx | pose.vy | pose.vz) != 0) && ClockTicAt(r.timeMS, bodyTic))
				{
					replay_AccNote(e, id);
					const ReplayAccWait w = { id, e.serial, bodyTic, pose.x, pose.y, pose.z, true };
					const int scored = replay_BodyAccScore(seg, e, w);
					if (scored == 0)
						waits.Push(w);
					else if (scored < 0)
						++seg.bodyAccUnresolved;
				}
			}
			++e.drawn;

			// Scored only where the velocity held over the step and moves it.
			if (IsBallistic(pose.misc) && e.balHave && (r.timeMS > e.balMS)
				&& (pose.vx == e.balVX) && (pose.vy == e.balVY) && (pose.vz == e.balVZ)
				&& ((pose.vx | pose.vy) != 0))
			{
				const double dt = (double)(r.timeMS - e.balMS) * TICRATE / 1000.0;
				const double dx = (double)pose.x - ((double)e.balX + (double)pose.vx * dt);
				const double dy = (double)pose.y - ((double)e.balY + (double)pose.vy * dt);
				const double dz = (double)pose.z - ((double)e.balZ + (double)pose.vz * dt);
				const double err = sqrt(dx * dx + dy * dy + dz * dz) / (double)FRACUNIT;
				ComputeBinAdd(seg.balHist, err);
				++seg.balSteps;
				seg.balErrSum += err;
				if (err > seg.balErrMax)
				{
					seg.balErrMax = err;
					seg.balWorstId = id;
					seg.balWorstMS = r.timeMS;
					seg.balWorstClass = GhostClassId(id);
					seg.balWorstExit = pszExit;
					seg.balWorstStart = bRefill ? refill[id].startExit : NULL;
					seg.balWorstBackdated = bRefill && refill[id].backdated;
					seg.balWorstAnchored = bRefill && refill[id].anchored;
					seg.balWorstSnapped = bRefill && refill[id].snapped;
					seg.balWorstSince = framesSinceRefill;
					seg.balWorstRefill = lastRefillKind;
				}
				if (err > 8.0)
					++seg.balOver8;
				if (err > 32.0)
					++seg.balOver32;
			}
			double accTic;
			if (IsBallistic(pose.misc) && ClockTicAt(r.timeMS, accTic))
			{
				replay_AccNote(e, id);
				const ReplayAccWait w = { id, e.serial, accTic, pose.x, pose.y, pose.z, false };
				const int scored = replay_AccScore(seg, e, w);
				if (scored == 0)
					waits.Push(w);
				else if (scored < 0)
					++seg.accUnresolved;
			}
			e.balX = pose.x; e.balY = pose.y; e.balZ = pose.z;
			e.balVX = pose.vx; e.balVY = pose.vy; e.balVZ = pose.vz;
			e.balMS = r.timeMS;
			e.balHave = true;

			// Against what the game drew, by sprite and frame, on each frame a
			// recorded appearance stands.
			if (e.haveDrawn)
			{
				const PClass* pType = NETWORK_GetClassFromIdentification(
					GhostClassId(id));
				const FState* pShown = (pType != NULL)
					? StateFromQState(pType, pose.stateIdx) : NULL;
				if (pShown != NULL)
				{
					++e.drawnRecords;
					++totalCmp;
					if ((pShown->sprite == e.lastSprite)
						&& ((pShown->GetFrame() & 0x3F) == (e.lastFrame & 0x3F)))
					{
						++e.drawnAgree;
						++totalAgree;
					}
				}
			}

			// Snap census: a single-frame jump large enough to see.
			if (e.hasLast && (r.timeMS > e.lastMS))
			{
				const unsigned int d = ComputeDistUnits(e.lastX, e.lastY, e.lastZ,
					pose.x, pose.y, pose.z);
				if (d > 64)
				{
					++e.jumps;
					if (d > e.worstJump)
						e.worstJump = d;
				}
			}

			// Glide census: straight-line displacement over a window against
			// the class's bound.
			if (e.winOpen == false)
			{
				e.winStartMS = r.timeMS;
				e.winStartX = pose.x; e.winStartY = pose.y; e.winStartZ = pose.z;
				e.winOpen = true;
			}
			else if ((r.timeMS - e.winStartMS) >= GLIDE_WINDOW_MS)
			{
				const unsigned int spd = SpeedForClass(GhostClassId(id));
				if (spd > 0)
				{
					const unsigned int elapsed = r.timeMS - e.winStartMS;
					const unsigned int d = ComputeDistUnits(e.winStartX, e.winStartY,
						e.winStartZ, pose.x, pose.y, pose.z);
					const double tics = (double)elapsed * (double)TICRATE / 1000.0;

					// Misc bits 5 and 6, the missile bits, pick the bound.
					const bool bMissile = (pose.misc & ((1 << 5) | (1 << 6))) != 0;
					const int pct100 = bMissile
						? (int)fua_csnap_glide_missile : (int)fua_csnap_glide_monster;
					const double capable = (double)spd * tics
						* ((double)pct100 / 100.0);
					const bool bOver = (capable > 0.0) && ((double)d > capable);
					const unsigned int pct = (capable > 0.0)
						? (unsigned int)(100.0 * (double)d / capable) : 0;

					// Served when the newest sample arrived inside the window.
					const bool bServed = (pose.arrivalAgeMS < elapsed);

					// The observed rate as a share of Speed per tic, unbounded.
					const unsigned int rawPct = (spd > 0)
						? (unsigned int)(100.0 * (double)d / ((double)spd * tics))
						: 0;
					e.classId = GhostClassId(id);

					if (bServed)
					{
						++e.winServed;
						if ((e.classId != 0) && (e.classId < CLASSRATE_MAX))
						{
							ReplayClassRate& k = classes[e.classId];
							k.pctSum += rawPct;
							++k.wins;
							if (rawPct > k.pctPeak)
								k.pctPeak = rawPct;
							if (bMissile)
								k.bMissile = true;
						}
						if (bOver)
						{
							++e.overServed;
							++e.overRun;
							if (e.overRun == 2)
								++e.glideEvents;
							if (pct > e.worstServedPct)
							{
								e.worstServedPct = pct;
								e.worstServedClass = e.classId;
							}
						}
						else
							e.overRun = 0;
					}
					else
					{
						++e.winStarved;
						if (bOver)
						{
							++e.overStarved;
							if (pct > e.worstStarvedPct)
							{
								e.worstStarvedPct = pct;
								e.worstStarvedClass = e.classId;
							}
						}
						// A starved window breaks a served run: the sampler
						// was not the thing drawing during it.
						e.overRun = 0;
					}
				}
				e.winOpen = false;		// reopened on the next frame
			}

			e.lastX = pose.x; e.lastY = pose.y; e.lastZ = pose.z;
			e.lastMS = r.timeMS;
			e.hasLast = true;

			if (pose.arrivalAgeMS > e.delayMaxMS)
				e.delayMaxMS = pose.arrivalAgeMS;
			e.delaySumMS += pose.arrivalAgeMS;

			if ((traceId >= 0) && (id == (unsigned int)traceId))
			{
				Printf("  %8u ms  x %7d y %7d z %6d  state %5u  age %5u ms  %s%s",
					r.timeMS, pose.x >> FRACBITS, pose.y >> FRACBITS,
					pose.z >> FRACBITS, (unsigned int)pose.stateIdx, pose.ageMS,
					(pszExit != NULL) ? pszExit : "?",
					(bRefill && (framesSinceRefill == 0)) ? " (refill)" : "");

				// On a refill's line, the anchor's inputs in fixed units, and the
				// exit the sample at the frame took.
				if (bRefill && (framesSinceRefill == 0))
				{
					const ReplayRefill& c = refill[id];
					Printf("  h-E %d,%d,%d  S-E %d,%d,%d  E-raw %d,%d,%d  frame %s",
						c.hMinusE[0], c.hMinusE[1], c.hMinusE[2],
						c.sMinusE[0], c.sMinusE[1], c.sMinusE[2],
						c.eMinusRaw[0], c.eMinusRaw[1], c.eMinusRaw[2],
						(c.hereExit != NULL) ? c.hereExit : "not drawable");
				}
				Printf("\n");
			}
		}
	}

	//*************************************************************************
	//	REPORT

	// The last span has no closing cvar change, so flush it here.
	replay_AccDrop(seg, waits);
	if (seg.frames > 0)
		replay_FlushSegment(seg, ents, classes, pCsv, bRefill);

	Printf("fua_csnap_analyze: %u deltas, %u frames, %u resets, %u decode errors, "
		"%u segments over %.1f s\n",
		deltas, frames, resets, applyErrors, cvarSwitches + 1,
		(lastMS > firstMS) ? ((double)(lastMS - firstMS) / 1000.0) : 0.0);

	// Class ids are the recording's numbering, so many unresolved warn.
	unsigned int idsSeen = 0, idsUnresolved = 0;
	for (unsigned int c = 1; c <= SNAP_CLASS_MASK; ++c)
	{
		if (classSeen[c] == 0)
			continue;
		++idsSeen;
		if (NETWORK_GetClassFromIdentification((unsigned short)c) == NULL)
			++idsUnresolved;
	}
	if ((idsUnresolved * 2) > idsSeen)
		Printf("fua_csnap_analyze: WARNING: %u of %u class ids do not resolve; load the "
			"recording's files in its order\n", idsUnresolved, idsSeen);

	// The harness against the game, a self-test at fua_csnap_localanim 0.
	if (drawnRecs != 0)
		Printf("fua_csnap_analyze: drawn state: %u recorded changes | replay agrees "
			"on %llu of %llu sampled frames (%.0f%%)\n",
			drawnRecs, (unsigned long long)totalAgree, (unsigned long long)totalCmp,
			(totalCmp > 0) ? (100.0 * (double)totalAgree / (double)totalCmp) : 0.0);
	if (otherRecs != 0)
		Printf("fua_csnap_analyze: %u records of a kind this build does not know\n",
			otherRecs);

	if (pCsv != NULL)
	{
		fclose(pCsv);
		Printf("fua_csnap_analyze: wrote %s\n", pszCsvPath);
	}

	// Restore, so the next measurement does not run in the recording's
	// configuration.
	for (unsigned int i = 0; i < savedNames.Size(); ++i)
	{
		FBaseCVar* pVar = FindCVar(savedNames[i].GetChars(), NULL);
		if (pVar != NULL)
		{
			UCVarValue v;
			v.Int = savedValues[i];
			pVar->SetGenericRep(v, CVAR_Int);
		}
	}
	Printf("fua_csnap_analyze: restored previous settings\n");

	CsnapClose(r);
	ClientReset();
	GhostClearAll();

	UseMapFrame(savedFrame);
	SetLawGravity(savedLaw);
	SetAnimatesLookup(savedAnimates);
	SetMoverSink(savedMover);
	SetLocalSink(savedLocal);
	SetPSpriteSink(savedPSprite);
}
