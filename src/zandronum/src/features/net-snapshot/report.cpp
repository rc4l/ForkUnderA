// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: report.cpp
//
// Description: The [snap] stats report: the window's lines, the queue they are
// printed from a line a pump, and the legacy traffic census. What the capture
// keeps for them is declared in report.h.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "doomdef.h"
#include "doomstat.h"	// gametic, which dates a window
#include "d_player.h"
#include "actor.h"
#include "c_cvars.h"
#include "c_console.h"	// C_GetMessageLevel and Logfile: where a report line ends up
#include "i_system.h"
#include "tarray.h"
#include "zstring.h"
#include "i_tictime.h"
#include "network.h"
#include "network/netcommand.h"	// GetStringSVC, naming a legacy command in the census
#include "sv_main.h"
#include "sv_commands.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/computation/budget_compute.h"	// SNAP_PACKET_OVERHEAD
#include "features/net-snapshot/computation/mover_compute.h"	// ComputeMoverClassName
#include "features/net-snapshot/capture.h"
#include "features/net-snapshot/clienttrack.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/ghostactor.h"
#include "features/net-snapshot/localpred.h"
#include "features/net-snapshot/movers.h"
#include "features/net-snapshot/polyobj.h"
#include "features/net-snapshot/report.h"

using namespace zx::netsnapshot;

// The open log file (c_console.cpp), for the notice of where the report goes.
extern FILE *Logfile;

EXTERN_CVAR(Int, fua_snap_stats)
EXTERN_CVAR(Int, fua_snap_nolegacy)
EXTERN_CVAR(Bool, fua_snap_movers)
EXTERN_CVAR(Bool, fua_snap_soundids)
EXTERN_CVAR(Bool, fua_snap_wirecensus)
EXTERN_CVAR(Bool, fua_snap_appear_predict)
EXTERN_CVAR(Bool, fua_snap_debug_refcheck)
EXTERN_CVAR(Int, fua_snap_animref_interval)
EXTERN_CVAR(Int, fua_snap_cosmetic_bytes)
EXTERN_CVAR(Int, fua_snap_cosmetic_events)
EXTERN_CVAR(Int, fua_snap_debug_ackdelay)
EXTERN_CVAR(Int, fua_snap_debug_holdback)
EXTERN_CVAR(Int, fua_snap_debug_loss)
EXTERN_CVAR(Int, fua_snap_entergrade)
EXTERN_CVAR(Int, fua_snap_missile_predict)

//*****************************************************************************
//	THE WINDOW

static BYTE				g_cosPrintedStamp = 0;
static bool				g_cosPrinted = false;

static int g_lastReportTic = 0;

static QWORD		g_evSpanTics = 0;		// game tics the event counters cover

// Everything the server put on the wire, both channels, so the snapshot path
// can be compared against what legacy replication still costs beside it.
static QWORD		g_outboundAtWindowStart = 0;
static bool			g_haveOutboundMark = false;

// One copy of the band names, shared by every report that names a band; the
// assertion on SNAPCL_TIERS_REPORT catches a missing entry at build time.
static const char* const g_szTierName[SNAPCL_TIERS_REPORT] =
{
	"overdue", "top", "enter", "MISSILE", "d0", "d1",
	"d2", "d3", "d4", "d5", "dlast", "refresh", "animref", "cosmetic"
};

//*****************************************************************************
//	THE QUEUE

static QWORD			g_drainRaw = 0, g_drainWorstRaw = 0;	// printing the previous report

// The report, printed a line a pump, since printed whole it takes longer than
// a tic and the catch-up moves every entity at once.
struct SnapSaid
{
	FString text;
	int printLevel;
};
static TArray<SnapSaid>	g_reportQueue;
static unsigned int		g_reportSkipped = 0;

// The drain, in place of Printf for anything printing inside the tic loop, formatting at once so
// the resets that follow are safe.
static void snap_Queue(int printLevel, const char* fmt, va_list ap)
{
	SnapSaid said;
	said.text.VFormat(fmt, ap);
	said.printLevel = printLevel;
	g_reportQueue.Push(said);
}

// A report line, for the log file alone: PRINT_LOW skips the server console window and RCON.
static void snap_Say(const char* fmt, ...) GCCPRINTF(1, 2);
static void snap_Say(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	snap_Queue(PRINT_LOW, fmt, ap);
	va_end(ap);
}

// A line the server's operator must see, on the console as well as in the log.
void zx::netsnapshot::snap_Warn(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	snap_Queue(PRINT_HIGH, fmt, ap);
	va_end(ap);
}

//*****************************************************************************
//	THE REPORT'S OWN HELPERS

// "Name n, Name n, Name n" for the three largest counts.
static void snap_CosTop3(const unsigned int* pByClass, char* psz, size_t size)
{
	psz[0] = '\0';
	if (pByClass == NULL)
		return;
	unsigned int topId[3] = { 0, 0, 0 }, topN[3] = { 0, 0, 0 };
	for (unsigned int c = 0; c < SNAP_MAXID; ++c)
	{
		const unsigned int n = pByClass[c];
		for (unsigned int k = 0; k < 3; ++k)
		{
			if (n <= topN[k])
				continue;
			for (unsigned int m = 2; m > k; --m) { topN[m] = topN[m - 1]; topId[m] = topId[m - 1]; }
			topN[k] = n;
			topId[k] = c;
			break;
		}
	}
	size_t at = 0;
	for (unsigned int k = 0; (k < 3) && (topN[k] > 0); ++k)
	{
		const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)topId[k]);
		const int wrote = mysnprintf(psz + at, size - at, "%s%s %u", (k > 0) ? ", " : "",
			(pType != NULL) ? pType->TypeName.GetChars() : "?", topN[k]);
		if ((wrote <= 0) || ((size_t)wrote >= size - at))
			break;
		at += (size_t)wrote;
	}
}

//*****************************************************************************
//	LEGACY TRAFFIC BY COMMAND

// Legacy traffic by command (see CountLegacyCommand). [0] reliable,
// [1] unreliable; index svc, or 256 + svc2 for an extended command.
#define SNAP_LEGACY_KEYS	512
static QWORD g_legacyCmdBytes[2][SNAP_LEGACY_KEYS];
static QWORD g_legacyCmdCount[2][SNAP_LEGACY_KEYS];
static QWORD g_legacyFlushBytes = 0, g_legacyFlushes = 0;
static QWORD g_legacyResendBytes = 0, g_legacyResends = 0;
static QWORD g_legacySendRaw = 0;
// The snapshot's own chunks also go out as a NetCommand; kept apart, as a
// cross-check against the outbound line's snapshot figure.
static QWORD g_legacySnapBytes = 0;

void zx::netsnapshot::CountLegacyCommand(int svc, int svc2, unsigned int bytes, bool bReliable)
{
	if ((svc == SVC_EXTENDEDCOMMAND) && (svc2 == SVC2_SNAPSHOT))
	{
		g_legacySnapBytes += bytes;
		return;
	}
	const int key = (svc == SVC_EXTENDEDCOMMAND) ? (256 + (svc2 & 0xFF)) : (svc & 0xFF);
	g_legacyCmdBytes[bReliable ? 0 : 1][key] += bytes;
	++g_legacyCmdCount[bReliable ? 0 : 1][key];
}

void zx::netsnapshot::CountLegacyFlush(unsigned int bytes)
{
	g_legacyFlushBytes += bytes;
	++g_legacyFlushes;
}

void zx::netsnapshot::CountLegacyResend(unsigned int bytes)
{
	g_legacyResendBytes += bytes;
	++g_legacyResends;
}

void zx::netsnapshot::CountLegacySendRaw(QWORD raw)
{
	g_legacySendRaw += raw;
}

static const char * snap_LegacyKeyName(int key)
{
	return (key >= 256) ? GetStringSVC2(static_cast<SVC2>(key - 256))
		: GetStringSVC(static_cast<SVC>(key));
}

// The top `n` commands by bytes, on channel 0 (reliable), 1 (unreliable) or
// both (-1).
static FString snap_LegacyTop(int channel, int n, double toK)
{
	FString top;
	bool used[SNAP_LEGACY_KEYS];
	memset(used, 0, sizeof(used));
	for (int i = 0; i < n; ++i)
	{
		int best = -1;
		QWORD bestBytes = 0;
		for (int k = 0; k < SNAP_LEGACY_KEYS; ++k)
		{
			const QWORD b = (channel == 0) ? g_legacyCmdBytes[0][k]
				: (channel == 1) ? g_legacyCmdBytes[1][k]
				: (g_legacyCmdBytes[0][k] + g_legacyCmdBytes[1][k]);
			if ((used[k] == false) && (b > bestBytes))
			{
				best = k;
				bestBytes = b;
			}
		}
		if (best < 0)
			break;
		used[best] = true;
		const QWORD count = (channel == 0) ? g_legacyCmdCount[0][best]
			: (channel == 1) ? g_legacyCmdCount[1][best]
			: (g_legacyCmdCount[0][best] + g_legacyCmdCount[1][best]);
		top.AppendFormat("%s%s %.1f kB/s (%llu%s)", (i > 0) ? " | " : "",
			snap_LegacyKeyName(best), (double)bestBytes * toK, (unsigned long long)count,
			((channel < 0) && (g_legacyCmdBytes[1][best] > g_legacyCmdBytes[0][best]))
				? ", unreliable" : "");
	}
	return top;
}

// The outbound line's "everything else" by command, in raw bytes a second,
// compared with the compressed wire figure by share, not summed; `seconds`
// spans g_stats' game tics, so a skipped report averages.
static void snap_ReportLegacyTraffic(double seconds)
{
	if (seconds <= 0.0)
		return;
	QWORD cmdRel = 0, cmdUnrel = 0;
	for (int k = 0; k < SNAP_LEGACY_KEYS; ++k)
	{
		cmdRel += g_legacyCmdBytes[0][k];
		cmdUnrel += g_legacyCmdBytes[1][k];
	}
	const double toK = 1.0 / (1024.0 * seconds);
	if ((cmdRel | cmdUnrel | g_legacyFlushBytes | g_legacyResendBytes) != 0)
	{
		const double outside = (g_legacyFlushBytes > cmdRel)
			? (double)(g_legacyFlushBytes - cmdRel) : 0.0;
		snap_Say("[snap] legacy traffic (raw): reliable %.1f kB/s = commands %.1f + outside"
			" NetCommand %.1f + headers %.1f | RESENT %.1f kB/s (%llu packets) | unreliable"
			" commands %.1f kB/s | send %.2f ms/tic | snapshot chunks %.1f kB/s\n",
			((double)g_legacyFlushBytes + 5.0 * (double)g_legacyFlushes) * toK,
			(double)cmdRel * toK, outside * toK, 5.0 * (double)g_legacyFlushes * toK,
			(double)g_legacyResendBytes * toK, (unsigned long long)g_legacyResends,
			(double)cmdUnrel * toK,
			snap_RawToMS(g_legacySendRaw) / (seconds * (double)TICRATE),
			(double)g_legacySnapBytes * toK);

		// The top ten by bytes, either channel, and the reliable top five,
		// which is what the reclassify order reads.
		snap_Say("[snap] legacy top: %s\n", snap_LegacyTop(-1, 10, toK).GetChars());
		if (cmdRel > 0)
			snap_Say("[snap] legacy top reliable: %s\n", snap_LegacyTop(0, 5, toK).GetChars());
	}
	memset(g_legacyCmdBytes, 0, sizeof(g_legacyCmdBytes));
	memset(g_legacyCmdCount, 0, sizeof(g_legacyCmdCount));
	g_legacyFlushBytes = g_legacyFlushes = 0;
	g_legacyResendBytes = g_legacyResends = 0;
	g_legacySendRaw = 0;
	g_legacySnapBytes = 0;
}

//*****************************************************************************
//	THE REPORT

// Where the report goes, said on the console once and again when it changes, since the report's
// own lines reach the log file alone.
static int				g_noticeStats = 0;	// 0 until the next report says where it goes
static const FILE *		g_noticeLog = NULL;
static int				g_noticeMsg = 0;

static void snap_ReportNotice()
{
	const int msg = (int)C_GetMessageLevel();
	if ((fua_snap_stats == g_noticeStats) && (Logfile == g_noticeLog) && (msg == g_noticeMsg))
		return;
	g_noticeStats = fua_snap_stats;
	g_noticeLog = Logfile;
	g_noticeMsg = msg;
	if (Logfile == NULL)
		snap_Warn("[snap] stats report on, but no log file is open: use logfile <name>\n");
	if (msg > PRINT_LOW)
		snap_Warn("[snap] stats report on, but msg is %d: its lines are dropped before the log"
			" file; set msg 0\n", msg);
	if ((Logfile != NULL) && (msg <= PRINT_LOW))
		snap_Warn("[snap] stats report every %d s, written to the log file\n",
			(int)fua_snap_stats);
}

void zx::netsnapshot::snap_MaybeReport()
{
	if (fua_snap_stats <= 0)
		g_noticeStats = 0;
	if ((fua_snap_stats <= 0) || (g_stats.frames == 0))
		return;

	// A threshold on elapsed tics, not a modulo, which catch-up would skip.
	if (gametic - g_lastReportTic < fua_snap_stats * TICRATE)
		return;

	// A report due while the last still drains is skipped and counted.
	if (g_reportQueue.Size() > 0)
	{
		++g_reportSkipped;
		return;
	}
	g_lastReportTic = gametic;
	snap_ReportNotice();

	const double f = (double)g_stats.frames;
	const double simRatio = (double)g_stats.simTics / f;	// 1.00 = holding 35 Hz
	// Rates are per second of game time, not per pump.
	const double gameSec = (double)g_stats.simTics / (double)TICRATE;
	const double pumpsPerSec = f / gameSec;
	// The event counters clear only when the branch block prints, so they can
	// span several windows; their game time and pumps are kept alongside.
	g_evSpanTics += g_stats.simTics;
	const double avgDelta = (double)g_stats.deltaBytes / f;
	// Zero whenever the compression above was gated off, so it is meaningful
	// only under the condition the print below branches on.
	const double avgZstd = (double)g_stats.zstdBytes / f;
	const double avgDirty = (double)g_stats.dirtyRecords / f;
	const double bytesPerDirty = (g_stats.dirtyRecords > 0)
		? (double)g_stats.deltaBytes / (double)g_stats.dirtyRecords : 0.0;
	const unsigned int budget = snap_EffectiveBudget();
	const int entsPerBudget = (bytesPerDirty > 0.0)
		? (int)((double)budget / bytesPerDirty) : 0;

	const double animPct = (g_stats.dirtyRecords > 0)
		? 100.0 * (double)g_stats.animOnly / (double)g_stats.dirtyRecords : 0.0;


	snap_Say("[snap] sim=%.2f t/capture%s | ents=%d (peak %u) awake=%d settled=%d anim-only=%.1f/frame (%.0f%% of dirty) enter=%.1f/frame leave=%.1f/frame"
		" | owned items not captured %.0f\n",
		simRatio, (simRatio > 1.05) ? " (SATURATED)" : "",
		(int)(g_stats.entities / g_stats.frames), g_stats.peakEntities,
		(int)(g_stats.awake / g_stats.frames), (int)(g_stats.settled / g_stats.frames),
		(double)g_stats.animOnly / f, animPct,
		(double)g_stats.enters / f, (double)g_stats.leaves / f,
		(g_huntingFrames != 0) ? (double)g_ownedAccum / (double)g_huntingFrames : 0.0);

	// Live MF_COUNTKILL actors holding a target: are the monsters awake.
	if (g_huntingFrames != 0)
	{
		snap_Say("[snap] monsters: %.0f live, %.0f hunting (%.0f%%)\n",
			(double)(QWORD)g_liveAccum / (double)g_huntingFrames,
			(double)(QWORD)g_huntingAccum / (double)g_huntingFrames,
			(g_liveAccum > 0)
				? (100.0 * (double)(QWORD)g_huntingAccum / (double)(QWORD)g_liveAccum) : 0.0);
	}
	// The top few entering classes, largest first, by selection sort.
	if (g_enterClassTotal > 0)
	{
		char szEnter[512];
		size_t at = 0;
		unsigned int shown = 0, named = 0;
		unsigned int pick[SNAP_ENTERCLASS_REPORT] = { 0 };

		for (unsigned int n = 0; n < SNAP_ENTERCLASS_REPORT; ++n)
		{
			unsigned int best = 0, bestId = 0;
			for (unsigned int c = 0; c < SNAP_MAXID; ++c)
			{
				if (g_enterByClass[c] <= best)
					continue;
				bool already = false;
				for (unsigned int k = 0; k < named; ++k)
					if (pick[k] == c) { already = true; break; }
				if (already == false) { best = g_enterByClass[c]; bestId = c; }
			}
			if (best == 0)
				break;
			pick[named++] = bestId;

			const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)bestId);
			const char* pszName = (pType != NULL) ? pType->TypeName.GetChars() : "?";
			const int wrote = snprintf(szEnter + at, sizeof(szEnter) - at, "%s%s %.1f/frame (%.0f%%)",
				(n > 0) ? ", " : "", pszName,
				(double)best / (double)g_stats.frames,
				100.0 * (double)best / (double)g_enterClassTotal);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szEnter) - at))
				break;
			at += (size_t)wrote;
			shown += best;
		}
		snap_Say("[snap] enter classes: %s | other %.0f%% of %u enters\n", szEnter,
			100.0 * (double)(g_enterClassTotal - shown) / (double)g_enterClassTotal,
			g_enterClassTotal);
	}

	// Standing population by class over captures, with the settled share.
	if ((g_popTotal > 0) && (g_stats.frames > 0) && (g_popByClass != NULL))
	{
		char szPop[512];
		size_t at = 0;
		unsigned int named = 0;
		unsigned int pick[SNAP_ENTERCLASS_REPORT] = { 0 };

			for (unsigned int n = 0; n < SNAP_ENTERCLASS_REPORT; ++n)
			{
				unsigned int best = 0, bestId = 0;
				for (unsigned int c = 0; c < SNAP_MAXID; ++c)
				{
					if (g_popByClass[c] <= best)
						continue;
					bool already = false;
					for (unsigned int k = 0; k < named; ++k)
						if (pick[k] == c) { already = true; break; }
					if (already == false) { best = g_popByClass[c]; bestId = c; }
				}
				if (best == 0)
					break;
				pick[named++] = bestId;

				const PClass * pType = NETWORK_GetClassFromIdentification((unsigned short)bestId);
				const char* pszName = (pType != NULL) ? pType->TypeName.GetChars() : "?";
				const double pop = (double)best / (double)g_stats.frames;
				const double set = (double)g_popSettledByClass[bestId] / (double)g_stats.frames;
				const int wrote = snprintf(szPop + at, sizeof(szPop) - at,
				"%s%s %.0f (%.0f%% settled)", (n > 0) ? ", " : "", pszName, pop,
				(pop > 0.0) ? (100.0 * set / pop) : 0.0);
				if ((wrote <= 0) || ((size_t)wrote >= sizeof(szPop) - at))
					break;
				at += (size_t)wrote;
			}
		snap_Say("[snap] population: %s | %.0f live, %.0f%% settled\n", szPop,
			(double)g_popTotal / (double)g_stats.frames,
			100.0 * (double)g_popSettledTotal / (double)g_popTotal);
	}

	// Cosmetic classes still holding NetIDs, and those a cosmetic's spawns
	// took.
	if ((g_stats.frames > 0) && (g_enterByClass != NULL) && (g_popByClass != NULL))
	{
		snap_CosEnsure();
		if ((g_cosPrinted == false) || (g_cosPrintedStamp != g_snapMapStamp))
		{
			g_cosPrinted = true;
			g_cosPrintedStamp = g_snapMapStamp;
			unsigned int nRow[SNAPCOS_ROWS] = { 0 };
			for (unsigned int c = 1; c < SNAP_MAXID; ++c)
				if (g_cosVerdict[c] != SNAPCOS_UNKNOWN)
					++nRow[snap_CosRow(c)];
			snap_Say("[snap] cosmetic classes: %u no server copy, %u server copy kept, %u inert, %u plain copy,"
				" %u networked, classified in %.1f ms (fua_snap_cosmetics lists them)\n", nRow[SNAPCOS_ROW_GONE],
				nRow[SNAPCOS_ROW_KEPT], nRow[SNAPCOS_ROW_INERT], nRow[SNAPCOS_ROW_PLAIN], nRow[SNAPCOS_ROW_NONE],
				g_cosClassifyMS);
		}

		unsigned int cosEnters = 0, cosLive = 0;
		unsigned int topId[3] = { 0, 0, 0 }, topN[3] = { 0, 0, 0 };
		for (unsigned int c = 0; c < SNAP_MAXID; ++c)
		{
			if (snap_CosIsCosmetic(c) == false)
				continue;
			cosEnters += g_enterByClass[c];
			cosLive += g_popByClass[c];
			const unsigned int n = g_enterByClass[c];
			for (unsigned int k = 0; k < 3; ++k)
			{
				if (n <= topN[k])
					continue;
				for (unsigned int m = 2; m > k; --m) { topN[m] = topN[m - 1]; topId[m] = topId[m - 1]; }
				topN[k] = n;
				topId[k] = c;
				break;
			}
		}
		char szTop[256];
		size_t at = 0;
		szTop[0] = '\0';
		for (unsigned int k = 0; (k < 3) && (topN[k] > 0); ++k)
		{
			const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)topId[k]);
			const int wrote = mysnprintf(szTop + at, sizeof(szTop) - at, "%s%s %.1f/frame",
				(k > 0) ? ", " : "", (pType != NULL) ? pType->TypeName.GetChars() : "?",
				(double)topN[k] / (double)g_stats.frames);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szTop) - at))
				break;
			at += (size_t)wrote;
		}

		// Networked children of a cosmetic spawner, top three by class.
		unsigned int inhId[3] = { 0, 0, 0 }, inhN[3] = { 0, 0, 0 };
		if (g_cosInheritByClass != NULL)
			for (unsigned int c = 0; c < SNAP_MAXID; ++c)
			{
				const unsigned int n = g_cosInheritByClass[c];
				for (unsigned int k = 0; k < 3; ++k)
				{
					if (n <= inhN[k])
						continue;
					for (unsigned int m = 2; m > k; --m) { inhN[m] = inhN[m - 1]; inhId[m] = inhId[m - 1]; }
					inhN[k] = n;
					inhId[k] = c;
					break;
				}
			}
		char szInh[256];
		size_t inhAt = 0;
		szInh[0] = '\0';
		for (unsigned int k = 0; (k < 3) && (inhN[k] > 0); ++k)
		{
			const PClass* pType = NETWORK_GetClassFromIdentification((unsigned short)inhId[k]);
			const int wrote = mysnprintf(szInh + inhAt, sizeof(szInh) - inhAt, "%s%s %u",
				(k > 0) ? ", " : "", (pType != NULL) ? pType->TypeName.GetChars() : "?", inhN[k]);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szInh) - inhAt))
				break;
			inhAt += (size_t)wrote;
		}

		snap_Say("[snap] cosmetic share: enters %.1f/frame (%.0f%% of enters), live %.0f (%.0f%%)%s%s"
			" | networked children of a cosmetic %.1f/frame%s%s (plus %.1f/frame already cosmetic)\n",
			(double)cosEnters / (double)g_stats.frames,
			(g_enterClassTotal > 0) ? (100.0 * (double)cosEnters / (double)g_enterClassTotal) : 0.0,
			(double)cosLive / (double)g_stats.frames,
			(g_popTotal > 0) ? (100.0 * (double)cosLive / (double)g_popTotal) : 0.0,
			(at > 0) ? " | top: " : "", szTop,
			(double)g_cosInheritTotal / (double)g_stats.frames,
			(inhAt > 0) ? ": " : "", szInh,
			(double)g_cosInheritOverlap / (double)g_stats.frames);
		if (g_cosInheritByClass != NULL)
			memset(g_cosInheritByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		g_cosInheritTotal = g_cosInheritOverlap = 0;
	}

	// Cosmetic events: what the event route sends (mode 2) or would send
	// (mode 1), against what the routed actors cost the snapshot now.
	if ((fua_snap_cosmetic_events > 0) && (g_stats.simTics > 0))
	{
		const double t = (double)g_stats.simTics;
		const double ct = (g_ceClientTics > 0) ? (double)g_ceClientTics : 1.0;
		QWORD generic = 0, todayBytes = 0, todayRecs = 0;
		for (unsigned int r = 0; r < SNAPCOS_ROWS; ++r)
		{
			generic += g_ceGeneric[r];
			todayBytes += g_ceTodayBytes[r];
			todayRecs += g_ceTodayRecs[r];
		}
		const QWORD seen = g_ceKept + g_ceCulled;
		snap_Say("[snap] cosmetic events: %.1f/tic made - rows 1-5 %.1f %.1f %.1f %.1f %.1f,"
			" blood %.1f, splatter %.1f | per client %.0f B/tic after culling (%.0f%% culled), against %.0f B/tic"
			" of snapshot records today (%.1f records/tic; rows 1-5 %.0f %.0f %.0f %.0f %.0f B/tic)\n",
			(double)(generic + g_ceBlood + g_ceSplatter) / t,
			(double)g_ceGeneric[1] / t, (double)g_ceGeneric[2] / t, (double)g_ceGeneric[3] / t,
			(double)g_ceGeneric[4] / t, (double)g_ceGeneric[5] / t,
			(double)g_ceBlood / t, (double)g_ceSplatter / t,
			(double)g_ceWanted / ct, (seen > 0) ? 100.0 * (double)g_ceCulled / (double)seen : 0.0,
			(double)todayBytes / ct, (double)todayRecs / ct,
			(double)g_ceTodayBytes[1] / ct, (double)g_ceTodayBytes[2] / ct, (double)g_ceTodayBytes[3] / ct,
			(double)g_ceTodayBytes[4] / ct, (double)g_ceTodayBytes[5] / ct);

		const QWORD sentPumps = g_ceAppended + g_ceAlone;
		snap_Say("[snap] cosmetic events cap: %d B/tic budget, %u B datagram | %.0f%% of client pumps capped"
			" (bank %.0f B when capped), %.0f B/tic sent | carried %.1f events/tic, dropped %.1f/tic (%.1f past age 7,"
			" %.1f over again, %.1f past a datagram) | %.0f%% join the last chunk, %.0f%% alone\n",
			(int)fua_snap_cosmetic_bytes, snap_PacketRoom() - SNAP_COSEVENT_HEADER,
			(g_ceClientPumps > 0) ? 100.0 * (double)g_cePumpsCapped / (double)g_ceClientPumps : 0.0,
			(g_cePumpsCapped > 0) ? (double)g_ceBankAtCap / (double)g_cePumpsCapped : 0.0,
			(double)g_ceSent / ct, (double)g_ceCarried / ct,
			(double)(g_ceCarryAged + g_ceCarryOver + g_ceCapDropped) / ct,
			(double)g_ceCarryAged / ct, (double)g_ceCarryOver / ct, (double)g_ceCapDropped / ct,
			(sentPumps > 0) ? 100.0 * (double)g_ceAppended / (double)sentPumps : 0.0,
			(sentPumps > 0) ? 100.0 * (double)g_ceAlone / (double)sentPumps : 0.0);
		snap_Say("[snap] cosmetic events hold: %.1f/tic held | released after the first Tick %.1f, by a script %.1f,"
			" at the deadline %.1f | removed while held %.1f | script changes after the event %.1f/tic | %u held now\n",
			(double)g_ceHeldN / t, (double)g_ceRelTick / t, (double)g_ceRelScript / t, (double)g_ceRelDeadline / t,
			(double)g_ceRemovedHeld / t, (double)g_ceScriptAfter / t, g_ceHeld.Size());

		// The not-inert flagged class spawned most (rows 2 and 4).
		unsigned int topId = 0, topN = 0;
		QWORD notInert = 0;
		if (g_ceNotInertByClass != NULL)
			for (unsigned int c = 0; c < SNAP_MAXID; ++c)
			{
				notInert += g_ceNotInertByClass[c];
				if (g_ceNotInertByClass[c] > topN)
				{
					topN = g_ceNotInertByClass[c];
					topId = c;
				}
			}
		const PClass* pTop = (topN > 0) ? NETWORK_GetClassFromIdentification((unsigned short)topId) : NULL;
		const double g = (generic > 0) ? (double)generic : 1.0;
		snap_Say("[snap] cosmetic events detail: velocity over 32 u/tic %.0f%%, under 1 %.0f%% | state byte %.0f%%"
			" (%.0f%% outside the chain) | extras %.0f%% | shooter skips %.1f/tic | age past 7 %.0f%% | from native Tick %.1f/tic"
			" | made by the spawner's copy %.1f/tic | gone before the flush %.1f/tic | untold puffs %.1f/tic"
			" | server copies removed %.1f/tic"
			" | relaxed missiles %.1f/tic | flagged, not inert %.1f/tic%s%s | blood originator unknown %.0f%%\n",
			100.0 * (double)g_ceVelBig / g, 100.0 * (double)g_ceVelSmall / g,
			100.0 * (double)g_ceStateByte / g,
			(g_ceStateByte > 0) ? 100.0 * (double)g_ceStateOutside / (double)g_ceStateByte : 0.0,
			100.0 * (double)g_ceExtras / g, (double)g_ceShooterSkips / ct,
			(g_ceKept > 0) ? 100.0 * (double)g_ceAgeSaturated / (double)g_ceKept : 0.0,
			(double)g_ceFromTick / t, (double)g_ceByCopy / t, (double)g_ceDiedEarly / t, (double)g_ceUntold / t,
			(double)g_ceRemoved / t,
			(double)g_ceRelaxed / t, (double)notInert / t,
			(pTop != NULL) ? ", top " : "", (pTop != NULL) ? pTop->TypeName.GetChars() : "",
			(g_ceBloodKept > 0) ? 100.0 * (double)g_ceBloodUnknown / (double)g_ceBloodKept : 0.0);

		// Layer 1 (layer-1 note): map things clients could make, the TID rule,
		// ACS-named classes and routed copies that outlive their event.
		char szListed[256], szSolid[256], szAge[256];
		snap_CosTop3(g_cmListedByClass, szListed, sizeof(szListed));
		snap_CosTop3(g_cmSolidByClass, szSolid, sizeof(szSolid));
		snap_Say("[snap] cosmetic map things: %u of %u networked map things clients could make%s%s%s, %u live"
			" | refused: marker %u, tid %u, special %u, args %u | solid, failing nothing else, %u%s%s%s"
			" | per client today: the listed %.1f records/tic, %.0f B/tic; the solid %.1f records/tic, %.0f B/tic\n",
			g_cmListed, g_cmNetworked, (szListed[0] != '\0') ? " (top: " : "", szListed,
			(szListed[0] != '\0') ? ")" : "", g_cmLive,
			g_cmRefused[0], g_cmRefused[1], g_cmRefused[2], g_cmRefused[3],
			g_cmSolidOnly, (szSolid[0] != '\0') ? " (top: " : "", szSolid, (szSolid[0] != '\0') ? ")" : "",
			(double)g_cmTodayRecs / ct, (double)g_cmTodayBytes / ct,
			(double)g_cmSolidRecs / ct, (double)g_cmSolidBytes / ct);
		unsigned int past5 = 0, past30 = 0, tidNow = 0;
		if (g_ceAgeByClass != NULL)
			memset(g_ceAgeByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		const unsigned int alive = (g_ceAgeByClass != NULL) ? snap_CosCountAlive(&past5, &past30, &tidNow) : 0;
		snap_CosTop3(g_ceAgeByClass, szAge, sizeof(szAge));
		const QWORD tidRefused = g_ceTidRule[1] + g_ceTidRule[2] + g_ceTidRule[3];
		snap_Say("[snap] cosmetic layer 1: the TID rule refuses %.1f/tic at the flush (tid %.1f, special %.1f,"
			" args %.1f), %.1f/tic children made by a refused copy's client copy | %u routed copies hold a tid now"
			" | ACS spawns routed %.1f/tic (%.1f +CLIENTSIDEONLY) | ACS names %u classes, %u keep a server copy"
			" (%.1f such copies/tic) | %u routed copies alive,"
			" %u past 5 s, %u past 30 s%s%s%s | %u joins, %.0f alive at each (worst %u)\n",
			(double)tidRefused / t, (double)g_ceTidRule[1] / t, (double)g_ceTidRule[2] / t,
			(double)g_ceTidRule[3] / t, (double)g_ceOrphaned / t, tidNow,
			(double)g_ceAcsRouted / t, (double)g_ceAcsClientSide / t,
			g_cosAcsClasses, g_cosAcsRow1, (double)g_ceAcsKept / t, alive, past5, past30,
			(szAge[0] != '\0') ? " (top: " : "", szAge, (szAge[0] != '\0') ? ")" : "",
			(unsigned int)g_ceJoins, (g_ceJoins > 0) ? (double)g_ceJoinAlive / (double)g_ceJoins : 0.0,
			(unsigned int)g_ceJoinWorst);
		snap_CosCountersReset();
	}

	// Reset here, with the unconditional print above, not in the block below.
	if (g_popByClass != NULL)
	{
		memset(g_popByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		memset(g_popSettledByClass, 0, SNAP_MAXID * sizeof(unsigned int));
	}
	g_popTotal = g_popSettledTotal = 0;

	// Compressed only while recording, so a 0 never stands for a compressor
	// that was off.
	if (g_recordFile != NULL)
	{
		snap_Say("[snap] delta avg=%dB zstd=%dB (peak %uB/%uB) | %.1f dirty/frame @ %.1fB | budget %dB fits ~%d ents (%.1f kB/s @ %d Hz)\n",
			(int)avgDelta, (int)avgZstd, g_stats.peakDelta, g_stats.peakZstd,
			avgDirty, bytesPerDirty, (int)budget, entsPerBudget,
			avgZstd * TICRATE / 1024.0, TICRATE);
	}
	else
	{
		snap_Say("[snap] delta avg=%dB zstd=off (peak %uB) | %.1f dirty/frame @ %.1fB | budget %dB fits ~%d ents (%.1f kB/s uncompressed @ %d Hz)\n",
			(int)avgDelta, g_stats.peakDelta,
			avgDirty, bytesPerDirty, (int)budget, entsPerBudget,
			avgDelta * TICRATE / 1024.0, TICRATE);
	}

	// Only off origin 0, wide or when something clamped, so a map that fits
	// prints what it always has.
	if ((g_frameClamps > 0)
		|| ((g_mapFrame.origin[0] | g_mapFrame.origin[1] | g_mapFrame.origin[2]) != 0)
		|| ((g_mapFrame.width[0] | g_mapFrame.width[1] | g_mapFrame.width[2]) != SNAP_FRAME_NARROW))
	{
		snap_Say("[snap] frame: origin %d %d %d, %u %u %u bytes an axis | %u positions clamped\n",
			g_mapFrame.origin[0], g_mapFrame.origin[1], g_mapFrame.origin[2],
			g_mapFrame.width[0], g_mapFrame.width[1], g_mapFrame.width[2], g_frameClamps);
	}

	// Where the dirty bytes go, by group, as a share of dirty records.
	if (g_stats.dirtyRecords > 0)
	{
		// In snap_TallyGroups' order, or every column shifts.
		static const char* const szGroup[SNAP_GROUP_COUNT] = { "pos", "posz", "ang", "velmisc", "appear", "render" };
		QWORD totalGrpBytes = 0;
		for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
			totalGrpBytes += g_stats.grpBytes[g];

		char szHist[256];
		size_t at = 0;
		szHist[0] = '\0';
		for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
		{
			const double recPct = 100.0 * (double)(QWORD)g_stats.grpRecs[g]
				/ (double)(QWORD)g_stats.dirtyRecords;
			const double bytePct = (totalGrpBytes > 0)
				? (100.0 * (double)(QWORD)g_stats.grpBytes[g] / (double)(QWORD)totalGrpBytes)
				: 0.0;
			const int wrote = mysnprintf(szHist + at, sizeof(szHist) - at,
				"%s%s %.0f%%/%.0f%%", (g > 0) ? " | " : "", szGroup[g], recPct, bytePct);
			if ((wrote <= 0) || ((size_t)wrote >= sizeof(szHist) - at))
				break;
			at += (size_t)wrote;
		}
		// Enters, left out of the histogram, are printed, so an all-enter
		// window's zeros read as such.
		if (g_brTotal > 0)
		{
			// The rate is world-wide, the histograms are per client: obs/branch
			// should read about the active client count.
			const double brObs = (g_branchCount > 0)
				? (double)g_brTotal / (double)g_branchCount : 0.0;
			snap_Say("[snap] branches: %.1f/frame world (%.1f/frame re-entry, %.1f/frame new"
				" occupant) | %u obs (%.1f/branch)"
				" | dist <512:%u <1k:%u <2k:%u <4k:%u <8k:%u far:%u\n",
				(double)g_branchCount / (double)(g_stats.frames > 0 ? g_stats.frames : 1),
				(double)g_reentryCount / (double)(g_stats.frames > 0 ? g_stats.frames : 1),
				(double)g_reentryReuse / (double)(g_stats.frames > 0 ? g_stats.frames : 1),
				g_brTotal, brObs,
				g_brDist[0], g_brDist[1], g_brDist[2], g_brDist[3], g_brDist[4], g_brDist[5]);
			snap_Say("[snap] branchpos (per-client obs): stale <2t:%u <5t:%u <10t:%u <35t:%u 35t+:%u"
				" | still:%u slow:%u fast:%u | deaths %u (%u within 512)\n",
				g_brStale[0], g_brStale[1], g_brStale[2], g_brStale[3], g_brStale[4],
				g_brVel[0], g_brVel[1], g_brVel[2], g_brDeaths, g_brDeathsNear);
			{
				unsigned int evQ, evS, evR, evXU, evXA, evF, evB, evSU, evSS;
				ServerEventStats(evQ, evS, evR, evXU, evXA, evF, evB, evSU, evSS);
				if ((evQ > 0) || (evS > 0))
					snap_Say("[snap] events: %u queued, %u sent, %u extra copies | "
						"EXPIRED unsent %u (share too small), acked-unknown %u | "
						"%u dropped full | superseded %u UNSENT, %u after"
						" delivery | %.1f kB/s\n",
						evQ, evS, evR, evXU, evXA, evF, evSU, evSS,
						(double)evB * (double)TICRATE / (1024.0 * (double)g_evSpanTics));

				{
					// Deaths apart, the one event failure visible in play.
					unsigned int evTS, evTX, evTR, evLU, evLP;
					EventTerminals(evTS, evTX, evTR, evLU, evLP);
					if ((evTS > 0) || (evTX > 0) || (evLU > 0) || (evLP > 0))
						snap_Say("[snap] event deaths: %u sent, %u spaced copies, %u EXPIRED"
							" unsent | at their entity's leave, deaths dropped: %u NEVER SENT,"
							" %u with later copies cut\n",
							evTS, evTR, evTX, evLU, evLP);
				}

				{
					unsigned int evV, evSD, evSF, evSS;
					EventSkips(evV, evSD, evSF, evSS);
					unsigned int evEmits, evSpentMax, evQMax, evBrk;
					QWORD evBudSum, evShareSum, evQSum;
					EventEmitStats(evEmits, evBudSum, evShareSum, evSpentMax, evQSum,
						evQMax, evBrk);
					// Per emit with events queued (one client, one
					// frame); spent is evB, over the same emits.
					if (evEmits > 0)
						snap_Say("[snap] event emit: %u emits | budget %.0f -> share %.0f B,"
							" spent %.0f (%.0f%% of share), max %u | queue %.1f, max %u"
							" | share stopped the loop in %u\n",
							evEmits, (double)evBudSum / evEmits, (double)evShareSum / evEmits,
							(double)evB / evEmits,
							(evShareSum > 0) ? 100.0 * (double)evB / (double)evShareSum : 0.0,
							evSpentMax, (double)evQSum / evEmits, evQMax, evBrk);

					if (evV > 0)
						snap_Say("[snap] event skips: %u visits | %u done, %u same-frame, "
							"%u share-blocked\n",
							evV, evSD, evSF, evSS);
				}
				ServerEventStatsReset();
				g_evSpanTics = 0;
			}


			if (g_straightSamples > 0)
			{
				snap_Say("[snap] straight runs (tics on one heading): missile 1:%u 2-3:%u"
					" 4-7:%u 8-15:%u 16+:%u | other 1:%u 2-3:%u 4-7:%u 8-15:%u 16+:%u\n",
					g_runMissile[0], g_runMissile[1], g_runMissile[2], g_runMissile[3],
					g_runMissile[4], g_runOther[0], g_runOther[1], g_runOther[2],
					g_runOther[3], g_runOther[4]);
				// Last tic's bit against this tic's heading: pred-TURNED is the
				// error that matters, step-held the opportunity cost.
				snap_Say("[snap] ballistic prediction: pred-held %u, PRED-TURNED %u, "
					"step-held %u, step-turned %u (%u missile samples, %.0f%% of "
					"extrapolated tics correct)\n",
					g_predHeld, g_predTurned, g_stepHeld, g_stepTurned,
					g_straightSamples,
					((g_predHeld + g_predTurned) > 0)
					? (100.0 * (double)g_predHeld / (double)(g_predHeld + g_predTurned)) : 0.0);
			}

		}

		snap_Say("[snap] dirty groups (%% of records / %% of group bytes): %s"
			" | %.0f dirty scored, %.1f enters/frame excluded\n", szHist,
			(double)(QWORD)g_stats.dirtyRecords / (double)g_stats.frames,
			(double)(QWORD)g_stats.enters / (double)g_stats.frames);
	}

	// Where the pump's time went, by phase, `sim` being a tic count, not ms.
	if (g_phaseFrames != 0)
	{
		// Per game tic covered, as the tic budget is, with per pump and worst
		// in ms as stalls of their own.
		const double pinv = 1.0 / (double)g_phaseFrames;
		const double tinv = pinv / simRatio;
		const double capMS = snap_RawToMS(g_phaseCaptureRaw) * tinv;
		const double sendMS = snap_RawToMS(g_phaseSendRaw) * tinv;
		const double tickMS = snap_RawToMS(g_phaseTickRaw) * tinv;
		snap_Say("[snap] pump: %.2f ms/tic snapshot = capture %.2f + perclient %.2f"
			" | %.0f%% of the 28.6 ms budget | per pump %.2f ms, worst %.1f\n",
			tickMS, capMS, sendMS,
			100.0 * tickMS / (1000.0 / (double)TICRATE),
			snap_RawToMS(g_phaseTickRaw) * pinv,
			snap_RawToMS(g_phaseWorstRaw));
	}
	// The pump's period against 28.6 ms, and the previous report's cost.
	if (g_pumpGapCount > 0)
	{
		snap_Say("[snap] pump gap: mean %.1f ms, worst %.1f ms spanning %d tics"
			" | previous report took %.1f ms, printing it %.1f ms (worst line %.1f), %u skipped\n",
			snap_RawToMS(g_pumpGapSumRaw) / (double)g_pumpGapCount,
			snap_RawToMS(g_pumpGapWorstRaw), g_pumpGapWorstTics,
			snap_RawToMS(g_reportRaw), snap_RawToMS(g_drainRaw), snap_RawToMS(g_drainWorstRaw),
			g_reportSkipped);
	}

	// The property pass, whose walk comes out of the capture figure above.
	{
		unsigned int live = 0, changes = 0, overflow = 0, truncated = 0;
		QWORD walks = 0, steps = 0;
		double sweepMS = 0.0;
		// Read before PropStats, which resets the window with the rest of them.
		const unsigned int marked = g_propMarked, told = g_propsTold;
		g_propsTold = 0;
		unsigned int sent = 0, cutByBudget = 0;
		PropWalkStats(sent, cutByBudget, true);
		PropStats(live, changes, overflow, truncated, walks, steps, sweepMS, true);
		const double tics = (g_stats.simTics > 0) ? (double)g_stats.simTics : 1.0;
		snap_Say("[snap] properties: %.0f walks stepping %.0f slots a tic, sweep %.3f ms/tic"
			" | %u entities off their class defaults, %u changes"
			" | %u marked, %u told, %u sent from a slot"
			" | %u OVER the table, %u names cut, %u walks cut by the budget\n",
			(double)walks / tics, (double)steps / tics, sweepMS / tics,
			live, changes, marked, told, sent, overflow, truncated, cutByBudget);
	}

	{
		// Psprite emission when it did anything, `capped` nonzero if clipped.
		unsigned int psEmit = 0, psCap = 0;
		ServerPSpriteStats(psEmit, psCap, true);
		if ((psEmit | psCap) != 0)
			snap_Say("[snap] psprite emit: %.2f records/frame across all clients"
				" | %u client-frames capped at %d\n",
				(f > 0.0) ? ((double)psEmit / f) : 0.0, psCap,
				(int)SNAPCL_PSPRITE_MAX_REPORT);

		// The local block whenever the pass ran, records a frame expected to
		// equal the client count, with `auth` (not `cfg`) what it did (10.1).
		SnapLocalEmitStats lo;
		ServerLocalStats(lo, true);
		if ((lo.emitted | lo.noPawn | lo.spectator | lo.noClient | lo.noId) != 0)
			snap_Say("[snap] local block: %.2f records/frame across all clients"
				" | auth %u/%u | cfg nolegacy %d"
				" | %u no-pawn | %u spectator | %u no-client | %u no-id\n",
				(f > 0.0) ? ((double)lo.emitted / f) : 0.0,
				lo.authoritative, lo.emitted, (int)fua_snap_nolegacy,
				lo.noPawn, lo.spectator, lo.noClient, lo.noId);

		// Movers, unconditionally (9.2), `changed` near `captures` or a binding
		// `capped` meaning the plane is being compared.
		{
			unsigned int mvLive = 0, mvCap = 0, mvBump = 0, mvFull = 0;
			unsigned int mvEmit = 0, mvCapped = 0, mvBlk = 0, mvEmitBlk = 0;
			unsigned int mvStair = 0, mvCls[SNAPMV_COUNT] = { 0 };
			unsigned int mvRecyc = 0, mvRecycPlane = 0, mvDisowned = 0;
			unsigned int mvRetired = 0, mvNarrowed = 0;
			unsigned int mvNoBytes = 0, mvSweptUnstarted = 0, mvSweptPressure = 0;
			unsigned int mvHeldPast = 0, mvHeldLongest = 0, mvHeldNow = 0;
			MoverCaptureStats(mvLive, mvCap, mvBump, mvFull, mvBlk,
				mvRecyc, mvRecycPlane, mvDisowned, mvRetired, mvNarrowed,
				mvSweptUnstarted, mvSweptPressure, mvHeldPast, mvHeldLongest,
				mvHeldNow, true);
			ServerMoverStats(mvEmit, mvCapped, mvEmitBlk, mvStair, mvCls,
				mvNoBytes, true);
			unsigned int mvPolyReclaimed = 0, mvPolyOverCap = 0, mvPolyResyncs = 0;
			MoverPolyStats(mvPolyReclaimed, mvPolyOverCap, mvPolyResyncs, true);

			// Only the classes that emitted, so the line does not grow a zero
			// column per class.
			char szCls[128];
			size_t nCls = 0;
			szCls[0] = '\0';
			for (int i = 0; i < SNAPMV_COUNT; i++)
			{
				if ((mvCls[i] == 0) || (nCls + 1 >= sizeof(szCls)))
					continue;
				mysnprintf(szCls + nCls, sizeof(szCls) - nCls, " %s %u",
					ComputeMoverClassName((BYTE)i), mvCls[i]);
				// strlen, not the return: mysnprintf reports the IDEAL length.
				nCls = strlen(szCls);
			}
			if (nCls == 0)
				mysnprintf(szCls, sizeof(szCls), " none");

			snap_Say("[snap] movers: cfg fua_snap_movers %d | %u live | %u captures"
				" %u changed | %u emitted (%u stair) by class:%s"
				" | frames ended: %u at the %d ceiling, %u out of bytes"
				" | %u refused, table full"
				" | slot reuse %u by address, %u by plane, %u disowned,"
				" %u retired, %u narrowed"
				" | swept %u unstarted, %u under table pressure"
				" | held past the old limit: %u released after it, %u still held,"
				" longest %u frames"
				" | blocked %u captured, %u emitted"
				" | poly slots reclaimed %u, %u entries over cap, %u resyncs\n",
				( fua_snap_movers ? 1 : 0 ), mvLive, mvCap, mvBump, mvEmit,
				mvStair, szCls,
				mvCapped, SNAPCL_MOVER_MAX, mvNoBytes, mvFull, mvRecyc,
				mvRecycPlane, mvDisowned, mvRetired, mvNarrowed, mvSweptUnstarted,
				mvSweptPressure, mvHeldPast, mvHeldNow, mvHeldLongest, mvBlk,
				mvEmitBlk, mvPolyReclaimed, mvPolyOverCap,
				mvPolyResyncs);

			// The legacy mover gate (7.1), `stopped` 0 for a class in use
			// meaning a sender it misses.
			unsigned int gtStop[SNAPMV_COUNT], gtPlanes = 0, gtResent = 0, gtResets = 0;
			unsigned int sndSector = 0, sndLegacy = 0;
			ServerMoverGateStats(gtStop, gtPlanes, gtResent, gtResets, true);
			MoverSoundStats(sndSector, sndLegacy, true);
			snap_Say("[snap] legacy movers: gate %d | stopped plat %u, floor %u,"
				" elevator %u, pillar %u, ceiling %u, door %u, poly %u"
				" (%u plane writes) | %u re-sent at gate off | %u table resets"
				" | mover sounds %u by sector, %u legacy\n",
				( MoverGate( ) ? 1 : 0 ), gtStop[SNAPMV_PLAT], gtStop[SNAPMV_FLOOR],
				gtStop[SNAPMV_ELEVATOR], gtStop[SNAPMV_PILLAR], gtStop[SNAPMV_CEILING],
				gtStop[SNAPMV_DOOR], gtStop[SNAPMV_POLY], gtPlanes, gtResent, gtResets,
				sndSector, sndLegacy);
		}

		// The MovePlayer gate, unconditionally, 0 with the cvar on meaning it
		// is not reached.
		{
			const QWORD skips = LegacyPlayerSkips(true);
			snap_Say("[snap] legacy MovePlayer: %llu client-frames suppressed\n",
				(unsigned long long)skips);
		}

		// Own-missile enter latency whenever a player fired, zero waits too.
		{
			unsigned int oh[SNAPCL_OWNENTER_BUCKETS_REPORT];
			unsigned int oN = 0, oW = 0, oSpawn = 0, oEvict = 0, oResent = 0;
			OwnEnterStats(oh, oN, oW, true);
			OwnMissileStats(oSpawn, oEvict, oResent, true);
			if ((oN | oSpawn) != 0)
			{
				const double inv = (oN > 0) ? (100.0 / (double)oN) : 0.0;
				snap_Say("[snap] own missile: %u spawned, %u enters sent"
					" | wait 0t %.0f%% 1t %.0f%% 2t %.0f%% 3-4t %.0f%%"
					" 5-8t %.0f%% 9t+ %.0f%% (worst %u tics)"
					" | %u re-sent | %u evicted\n",
					oSpawn, oN,
					oh[0] * inv, oh[1] * inv, oh[2] * inv,
					oh[3] * inv, oh[4] * inv, oh[5] * inv,
					oW, oResent, oEvict);
			}
		}
	}


	// Immediately after its own print, and outside every gate below, so no
	// gate can skip the reset.
	g_phaseCaptureRaw = g_phaseSendRaw = g_phaseTickRaw = g_phaseWorstRaw = 0;
	g_phaseFrames = 0;
	g_pumpGapSumRaw = g_pumpGapWorstRaw = g_reportRaw = 0;
	g_drainRaw = g_drainWorstRaw = 0;
	g_pumpGapCount = 0;
	g_pumpGapWorstTics = 0;
	g_reportSkipped = 0;

	if (g_sentPackets != 0)
	{
		snap_Say("[snap] sent %u packets, %.1f kB (%.1f kB/s)\n",
			g_sentPackets, g_sentBytes / 1024.0,
			(g_sentBytes / 1024.0) / gameSec);

		// [Kaori-Chani] Every client in turn, not slot 0: one client settling
		// says nothing about another's, and slot 0 may be the one that left.
		for (unsigned int c = 0; c < MAXPLAYERS; ++c)
		{
			if (SERVER_IsValidClient(c) == false)
				continue;
			// lag is how far behind this client's newest datagram its ack is, 255 being far or
			// further and 0 none credited, by no arrival or a re-baseline.
			snap_Say("[snap] client %u: ack=%u lag=%u pending=%u believed=%u\n",
				c, LastAckedSeq(c), (unsigned int)AckLag(c, NewestSentSeq(c)),
				PendingFrames(c), BelievedCount(c));
		}

		// Every client's health at once, max pending the signal and any
		// rebaseline worth investigating.
		SnapClientHealth health;
		AggregateHealth(health);
		snap_Say("[snap] clients: %u active | believed avg %u max %u | "
			"pending avg %.1f max %u | %u rebaselines\n",
			health.activeClients, health.believedAvg, health.believedMax,
			health.pendingAvg, health.pendingMax, health.rebaselines);

		// The contested set by distance ring and by in-front, which is what
		// sizes interest management.
		if (g_priorityFrames != 0)
		{
			SnapVisibilityCensus c;
			GetCensus(c);
			const double inv = 1.0 / (double)g_priorityFrames;

			// Fixed scratch, its cursor clamped, since mysnprintf returns
			// the would-be length.
			char dbuf[128]; unsigned int dn = 0;
			char fbuf[128]; unsigned int fn = 0;
			for (int i = 0; i < c.buckets; ++i)
			{
				if (dn < sizeof(dbuf) - 1)
				{
					const int w = mysnprintf(dbuf + dn, sizeof(dbuf) - dn, "%s%.0f",
						(i ? " " : ""), (double)c.byDist[i] * inv);
					if (w > 0)
						dn += (unsigned int)w;
				}
				if (fn < sizeof(fbuf) - 1)
				{
					const int w = mysnprintf(fbuf + fn, sizeof(fbuf) - fn, "%s%.0f",
						(i ? " " : ""), (double)c.frontByDist[i] * inv);
					if (w > 0)
						fn += (unsigned int)w;
				}
			}
			if (dn >= sizeof(dbuf)) dbuf[sizeof(dbuf) - 1] = 0;
			if (fn >= sizeof(fbuf)) fbuf[sizeof(fbuf) - 1] = 0;

			snap_Say("[snap] census: %.0f contested/frame, %.0f in front (%.0f%%) | "
				"by ring [%s] | front by ring [%s] (rings 512/1024/2048/4096/8192)\n",
				(double)c.contested * inv, (double)c.front * inv,
				(c.contested > 0) ? (100.0 * (double)c.front / (double)c.contested) : 0.0,
				dbuf, fbuf);
		}

		// The refresh rate an entity sees, averaged over the whole world; the
		// per-band line below answers the exit criterion.
		if (g_priorityFrames != 0)
		{
			const double inv = 1.0 / (double)g_priorityFrames;

			double emittedRecs = 0.0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				emittedRecs += (double)g_tierAccum[t] * inv;

			// For the deferred/withheld reporting below, not for `contested`.
			const double deferredRecs = (double)(QWORD)g_deferredAccum * inv;
			const double withheld = (double)(QWORD)g_cosmeticAccum * inv;
			(void)deferredRecs; (void)withheld;

			// Everything ranked and competing, summed directly over the
			// ranked tiers rather than by difference.
			double contested = 0.0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				contested += (double)g_tierSizeAccum[t] * inv;
			const double fresh = (double)(QWORD)g_freshAccum * inv;
			// `rate` against `fresh` (its new share): a low rate is a budget
			// problem, a low fresh share an ack one.
			const double rate = (contested > 1.0)
				? (emittedRecs * pumpsPerSec / contested) : pumpsPerSec;
			const double freshShare = (emittedRecs > 0.0) ? (100.0 * fresh / emittedRecs) : 0.0;

			// `resend`, records carrying only unacked groups, whose bytes a
			// shorter ack round trip would buy back.
			const double resendRecs = (double)(QWORD)g_resendAccum * inv;
			const double resendB = (double)(QWORD)g_resendByteAccum * inv;

			snap_Say("[snap] refresh: %.1f Hz per contested entity (%.0f contested, "
				"%.1f rec/frame, %.0f%% fresh) | resend %.1f rec/f (%.0f B/f) "
				// Injected loss and delay; observed loss: datagram delivery.
				"| inject ackdelay %d, loss %d%% (%u dropped), holdback %d%% (%u held)\n",
				rate, (contested > 0.0) ? contested : 0.0, emittedRecs, freshShare,
				resendRecs, resendB,
				(int)fua_snap_debug_ackdelay, (int)fua_snap_debug_loss, g_droppedPackets,
				(int)fua_snap_debug_holdback, g_heldPackets);

			// Per-band refresh, the exit criterion's quantity: records the band
			// sent over entities ranked into it.
			{
				char szRate[256];
				size_t at = 0;
				szRate[0] = '\0';
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					const double sz = (double)(QWORD)g_tierSizeAccum[t] * inv;
					if (sz < 0.5)
						continue;			// band empty this window; omit it
					const double rec = (double)(QWORD)g_tierAccum[t] * inv;
					const double hz = rec * pumpsPerSec / sz;
					const int wrote = mysnprintf(szRate + at, sizeof(szRate) - at,
						"%s%s %.1f/%.0f", (at > 0) ? " | " : "", g_szTierName[t], hz, sz);
					if ((wrote <= 0) || ((size_t)wrote >= sizeof(szRate) - at))
						break;
					at += (size_t)wrote;
				}
				if (at > 0)
					snap_Say("[snap] band Hz/entities: %s\n", szRate);

				// Reserve use: `spent` under `allowed` wastes d0's bytes, and
				// `capped` bands bind as designed, `drained` ones oversized.
				if (g_resAllowAccum != 0)
				{
					const double allow = (double)(QWORD)g_resAllowAccum * inv;
					const double spent = (double)(QWORD)g_resSpentAccum * inv;
					snap_Say("[snap] reserve: %.0f B allowed, %.0f B spent (%.0f%%)"
						" | bands capped %.1f, drained %.1f\n",
						allow, spent, (allow > 0.0) ? (100.0 * spent / allow) : 0.0,
						(double)(QWORD)g_resCappedAccum * inv,
						(double)(QWORD)g_resDrainedAccum * inv);

				}
			}
		}

		// Both channels together: the only figure that shows what the legacy
		// suppression gate is worth.
		if (g_haveOutboundMark)
		{
			const QWORD total = SERVER_STATISTIC_GetTotalOutboundDataTransferred();
			const double windowBytes = (double)(QWORD)(total - g_outboundAtWindowStart);
			const double snapshotBytes = (double)(QWORD)g_wireSentBytes;
			const double legacyShare = (windowBytes > 0.0)
				? (100.0 * (windowBytes - snapshotBytes) / windowBytes) : 0.0;

			snap_Say("[snap] server outbound: %.1f kB/s total | snapshot %.1f kB/s | "
				"everything else %.1f kB/s (%.0f%%) | nolegacy %d\n",
				(windowBytes / 1024.0) / gameSec,
				(snapshotBytes / 1024.0) / gameSec,
				((windowBytes - snapshotBytes) / 1024.0) / gameSec,
				legacyShare, (int)fua_snap_nolegacy);
		}
		snap_ReportLegacyTraffic(gameSec);

		// What those packets cost on the wire, and so the room the MTU leaves.
		if (g_wirePackets != 0)
		{
			const double payload = (double)(QWORD)g_wirePayloadBytes / (double)g_wirePackets;
			const double wire = (double)(QWORD)g_wireSentBytes / (double)g_wirePackets;
			const double ratio = (wire > 0.0) ? (payload / wire) : 0.0;
			const unsigned int mtu = (unsigned int)SERVER_GetMaxPacketSize();

			// Per DATAGRAM: how much bigger a CHUNK could be, not the budget,
			// which grows by adding datagrams.
			const int headroom = (ratio > 0.0)
				? (int)((double)mtu * ratio) - SNAP_PACKET_OVERHEAD : 0;

			snap_Say("[snap] wire: %.0fB payload -> %.0fB sent (%.2fx, peak %uB, zstd %u/%u) "
				"| %.1f kB/s actual | chunk could be ~%dB at sv_maxpacketsize %u\n",
				payload, wire, ratio, g_wirePeak, g_wireZstdPackets, g_wirePackets,
				((double)(QWORD)g_wireSentBytes / 1024.0) / gameSec,
				(headroom > 64) ? headroom : 64, mtu);
		}

		// `chunks` built and datagrams `launched` must be equal, or chunks
		// shared a datagram.
		if (g_chunkFrames != 0)
			snap_Say("[snap] chunking: %.2f chunks/frame, %llu chunks vs %llu launched "
				"| budget %uB over chunks of %uB\n",
				(double)(QWORD)g_chunkDatagrams / (double)(QWORD)g_chunkFrames,
				(unsigned long long)g_chunkDatagrams,
				(unsigned long long)g_wirePackets,
				snap_EffectiveBudget(), snap_ChunkLimit());

		// Where the budget went, averaged over the window.
		if (g_priorityFrames != 0)
		{
			const double inv = 1.0 / (double)g_priorityFrames;
			double emitted = 0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				emitted += (double)g_tierAccum[t] * inv;

			// One entry per tier, asserted against the count rather than a
			// literal, so only adding the name satisfies it.
			typedef char snapTierNamesMatchTiers
				[(sizeof(g_szTierName) / sizeof(g_szTierName[0]) == SNAPCL_TIERS_REPORT) ? 1 : -1];
			(void)sizeof(snapTierNamesMatchTiers);

			// Built from g_szTierName rather than a format string, so no band
			// goes unprinted.
			{
				char szPri[320];
				size_t at = 0;
				szPri[0] = '\0';
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					const int wrote = mysnprintf(szPri + at, sizeof(szPri) - at,
						"%s%s %.0f", (t > 0) ? " " : "", g_szTierName[t],
						(double)g_tierAccum[t] * inv);

					if ((wrote <= 0) || ((size_t)wrote >= sizeof(szPri) - at))
						break;

					at += (size_t)wrote;
				}
				 snap_Say("[snap] priority/frame: %s = %.0f recs\n", szPri, emitted);
			}

			// A high forced share means the budget is so oversubscribed that
			// priority has degenerated toward round-robin.
			snap_Say("[snap] sound events: %u sent (%u positional, %u from routed cosmetics), %u culled (%.0f%% inaudible)\n",
			g_soundSent, g_soundPositional, g_soundRoutedPoint, g_soundCulled,
			// Widen BEFORE adding: the sum of two unsigned ints is computed in
			// 32 bits and only then converted (MSVC C26451).
			((double)g_soundSent + (double)g_soundCulled > 0.0)
				? (100.0 * (double)g_soundCulled
					/ ((double)g_soundSent + (double)g_soundCulled)) : 0.0);
		g_soundSent = g_soundCulled = g_soundPositional = g_soundRoutedPoint = 0;

		// Per client, for this map's stamp: matched takes ids, DIFFER keeps
		// names for the whole map, unanswered has not acked since the table.
		{
			unsigned int matched = 0, differ = 0, unanswered = 0;
			for (unsigned int i = 0; i < MAXPLAYERS; ++i)
			{
				if (snap_ClientInMap(i) == false)
					continue;
				const BYTE a = g_soundIdsAnswer[i];
				if (((a & SNAP_SOUNDIDS_ANSWERED) == 0)
					|| ((a & SNAP_SOUNDIDS_STAMP) != (g_snapMapStamp & SNAP_SOUNDIDS_STAMP)))
					++unanswered;
				else if (a & SNAP_SOUNDIDS_MATCH)
					++matched;
				else
					++differ;
			}
			snap_Say("[snap] sound ids: %u by id, %u by name (%u id range, %u channel,"
				" %u switch off, %u client not matched) | clients %u matched,"
				" %u DIFFER (names), %u unanswered | fua_snap_soundids %d\n",
				g_soundById, g_soundByName, g_soundNameRange, g_soundNameChannel,
				g_soundNameOff, g_soundNameUnmatched,
				matched, differ, unanswered, (int)fua_snap_soundids);
			// Never audible anywhere, per S_Sound call before the fan-out,
			// apart from culled.

			snap_Say("[snap] sounds screened: %u unplayable calls (no sound or volume 0)\n",
				g_soundUnplayable);
			g_soundById = g_soundByName = 0;
			g_soundNameRange = g_soundNameChannel = g_soundNameOff = g_soundNameUnmatched = 0;
			g_soundUnplayable = 0;
		}

		// The clamp: printed only when it dropped something, since a server keeping up has
		// nothing to say here and the line would read as a problem.
		{
			unsigned int dropped = 0, pumps = 0, worst = 0, runs = 0;
			int worstPercent = 100;
			snap_CatchUpStats(dropped, pumps, worst, runs, worstPercent);
			if (dropped > 0)
				snap_Say("[snap] catch-up: %u tics DROPPED over %u pumps (worst %u in one)"
					" | %u runs | game time %d%% of real at its slowest\n",
					dropped, pumps, worst, runs, worstPercent);
		}

		g_branchCount = g_brTotal = g_brDeaths = g_brDeathsNear = 0;
		g_reentryCount = g_reentryReuse = 0;
		memset(g_brDist, 0, sizeof(g_brDist));
		memset(g_brStale, 0, sizeof(g_brStale));
		memset(g_brVel, 0, sizeof(g_brVel));
		memset(g_runMissile, 0, sizeof(g_runMissile));
		memset(g_runOther, 0, sizeof(g_runOther));
		g_predHeld = g_predTurned = g_stepHeld = 0;
		g_stepTurned = g_straightSamples = 0;

		unsigned int tSeen = 0, tProm = 0, tCap = 0, tEmit = 0;
		TerminalStats(tSeen, tProm, tCap, tEmit, true);
		if ((tSeen | tProm | tCap | tEmit) != 0)
		{
			snap_Say("[snap] terminal sequences: %u flagged, %u promoted, %u over"
				" ceiling, %u EMITTED (max %d/frame)\n",
				tSeen, tProm, tCap, tEmit, (int)SNAPCL_TERMINAL_MAX);
		}

		// Outside the death gate above, since both mechanisms run every
		// frame and their stats calls reset.
		{
			unsigned int pHeld, pSent, pExp, pWorst;
			PredictStats(pHeld, pSent, pExp, pWorst, true);
			unsigned int lHeld, lPos, lVel, lExp, lWorst;
			ServerLawStats(lHeld, lPos, lVel, lExp, lWorst, true);
			// Printed whenever the cvar is on, INCLUDING at zero, so "on and
			// suppressing nothing" and "off" are not one line.
			if (fua_snap_missile_predict > 0)
				snap_Say("[snap] missile predict: %u held, %u sent (drifted),"
					" %u past bridge | worst held %uu, tolerance %du | gravity class: %u held,"
					" sent on position %u, on velocity %u, %u past bridge, worst held %uu\n",
					pHeld, pSent, pExp, pWorst, (int)fua_snap_missile_predict,
					lHeld, lPos, lVel, lExp, lWorst);
		}

		{
			unsigned int aHeld[SNAPCL_APOP_COUNT], aDiv[SNAPCL_APOP_COUNT];
			unsigned int aUnw[SNAPCL_APOP_COUNT];
			AppearStats(aHeld, aDiv, aUnw, true);
			// Whenever the cvar is on, a line per population, as a share of
			// the decisions, which the client's probe cannot all see.
			if (fua_snap_appear_predict)
			{
				for (unsigned int ap = 0; ap < SNAPCL_APOP_COUNT; ++ap)
				{
					const unsigned int aTot = aHeld[ap] + aDiv[ap] + aUnw[ap];
					snap_Say("[snap] appear predict %s: %u held (%u%%),"
						" %u diverged, %u unwalkable of %u decisions\n",
						SNAPCL_AppearPopNames[ap], aHeld[ap],
						aTot ? (aHeld[ap] * 100 / aTot) : 0, aDiv[ap],
						aUnw[ap], aTot);
				}
			}
		}

		{
			unsigned int rSeen = 0, rHist[5];
			ReentryStats(rSeen, rHist, true);
			if (rSeen != 0)
				snap_Say("[snap] re-entry: %u returned from held"
					" | silence <1s %u <3s %u <6s %u <12s %u 12s+ %u\n",
					rSeen, rHist[0], rHist[1], rHist[2], rHist[3], rHist[4]);

			{
				unsigned int bRecs[SNAPCL_TIERS_REPORT], bPos[SNAPCL_TIERS_REPORT];
				BandComposition(bRecs, bPos, true);
				char szBand[512];
				size_t at = 0;
				bool any = false;
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					if (bRecs[t] == 0)
						continue;
					any = true;
					const int wrote = snprintf(szBand + at, sizeof(szBand) - at,
						"%s%s %.0f%%", (at > 0) ? " | " : "", g_szTierName[t],
						100.0 * (double)bPos[t] / (double)bRecs[t]);
					if ((wrote <= 0) || ((size_t)wrote >= sizeof(szBand) - at))
						break;
					at += (size_t)wrote;
				}
				if (any)
					snap_Say("[snap] band POS share: %s\n", szBand);
			}

			unsigned int oG[SNAP_GROUP_COUNT], oS[4], oP[4], oTot = 0;
			OwedStats(oG, oS, oP, oTot, true);
			if (oTot > 0)
			{
				snap_Say("[snap] owed groups: pos %.0f%% posz %.0f%% ang %.0f%%"
					" velmisc %.0f%% appear %.0f%% render %.0f%% (%u contested)\n",
					100.0 * oG[0] / oTot, 100.0 * oG[1] / oTot, 100.0 * oG[2] / oTot,
					100.0 * oG[3] / oTot, 100.0 * oG[4] / oTot, 100.0 * oG[5] / oTot,
					oTot / (g_stats.frames ? g_stats.frames : 1));

				snap_Say("[snap] owed age: <1s %.0f%% <3s %.0f%% <10s %.0f%%"
					" 10s+ %.0f%% || owing POS: <1s %u <3s %u <10s %u 10s+ %u\n",
					100.0 * oS[0] / oTot, 100.0 * oS[1] / oTot, 100.0 * oS[2] / oTot,
					100.0 * oS[3] / oTot,
					oP[0] / (g_stats.frames ? g_stats.frames : 1),
					oP[1] / (g_stats.frames ? g_stats.frames : 1),
					oP[2] / (g_stats.frames ? g_stats.frames : 1),
					oP[3] / (g_stats.frames ? g_stats.frames : 1));
			}
		}

		unsigned int aSeen = 0, aLost = 0, aWide = 0, aFolded = 0, aLate = 0, aSetAfter = 0;
		AckLossStats(aSeen, aLost, aWide, aFolded, aLate, aSetAfter, true);
		const unsigned int acksAhead = AcksAhead(true);
			if (aSeen > 0)
				snap_Say("[snap] datagram delivery: %u sent datagrams confirmed, %u LOST"
					" (%.2f%%), %u late, %u gaps beyond the window, %u folded on evidence"
					" | %u set after declared lost | %u acks ahead of sent\n",
					aSeen, aLost,
					100.0 * (double)aLost / (double)aSeen, aLate, aWide, aFolded, aSetAfter,
					acksAhead);
			else if (acksAhead > 0)
				snap_Say("[snap] datagram delivery: %u acks ahead of sent\n", acksAhead);

		// The reference check, while fua_snap_debug_refcheck runs it.
		{
			unsigned int rcF, rcFd, rcR, rcRd, rcP, rcPd;
			ReferenceCheckStats(rcF, rcFd, rcR, rcRd, rcP, rcPd, true);
			if ((rcF + rcR + rcP) > 0)
				snap_Say("[snap] reference check: %u in flight, %u DIFFER | %u at rest,"
					" %u DIFFER | %u psprites, %u DIFFER\n", rcF, rcFd, rcR, rcRd, rcP, rcPd);
		}

		{
			unsigned int mM = 0, mL = 0, mE = 0;
			MaybeStats(mM, mL, mE, true);
			if ((mM | mL | mE) != 0)
				snap_Say("[snap] unconfirmed enters: %u ids marked (lost without proof,"
					" not believed present) | %u leaves sent for them | %u cleared by a"
					" confirmed enter\n", mM, mL, mE);
		}
		{
			unsigned int rF = 0, rE = 0, rM = 0, rP = 0;
			RepairStats(rF, rE, rM, rP, true);
			if (rF > 0)
				snap_Say("[snap] optimistic: repaired %u frames,"
					" %u entities, %u movers, %u property slots\n", rF, rE, rM, rP);
		}

		// The marker count now, not over the window, staying up when an id
		// is sent an enter every frame.
		{
			unsigned int reenters = 0, onPresent = 0;
			PresenceStats(reenters, onPresent, true);
			snap_Say("[snap] presence: %u re-enters while a leave was in flight"
				" | %u leave markers on a present id\n", reenters, onPresent);
		}

		{
			unsigned int sent = 0;
			TargetStats(sent, true);
			snap_Say("[snap] missile target: world enters with one %u, spawner missiles with none %u"
				" | sent %.1f/frame, +%.0f B/frame\n",
				g_targetEnters, g_targetNoneEnters, (double)sent * inv, 2.0 * (double)sent * inv);
			g_targetEnters = g_targetNoneEnters = 0;
		}

		snap_Say("[snap] effects: %u captured actors carry effects (bits 0x%02x)\n",
			g_capEffectsActors, (unsigned int)g_capEffectsBits);
			g_capEffectsActors = 0;
			g_capEffectsBits = 0;

		snap_Say("[snap] priority: aging-forced %.0f%% | cosmetic held %.0f/frame | deferred %.0f/frame"
				" | entergrade %d\n",
				(emitted > 0) ? (100.0 * (double)g_forcedAccum * inv / emitted) : 0.0,
				(double)g_cosmeticAccum * inv, (double)g_deferredAccum * inv,
				(int)(bool)fua_snap_entergrade);

		// The quota the code used, not one derived again for the report.
		{
			unsigned int quota = 0, wanted = 0, pop = 0;
			AnimrefQuota(quota, wanted, pop);
			snap_Say("[snap] animref quota: %u/frame over %u held -> ~%.1f s"
					" | target %.1f s | ceiling %d%s\n",
					quota, pop,
					(quota > 0) ? ((double)pop / ((double)quota * pumpsPerSec)) : 0.0,
					(double)(int)fua_snap_animref_interval / (double)TICRATE,
					(int)SNAPCL_ANIMREF_MAX,
					(wanted > 0) ? " (CLAMPED)" : "");
		}

		// The encoding census, over records as written, not as captured.
		if (fua_snap_wirecensus)
		{
			SnapWireCensusOut wc;
			WireCensus(wc, true);
			QWORD recs = 0, bytes = 0, appOnly = 0, grpTotal = 0;
			for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
			{
				recs += wc.recs[t];
				bytes += wc.bytes[t];
				appOnly += wc.appOnly[t];
			}
			for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
				grpTotal += wc.grpBytes[g];

			if (recs > 0)
			{
				static const char* const szGroup[SNAP_GROUP_COUNT] =
					{ "pos", "posz", "ang", "velmisc", "appear", "render" };
				char sz[320];
				size_t at = 0;
				sz[0] = '\0';
				for (unsigned int g = 0; g < SNAP_GROUP_COUNT; ++g)
				{
					const int w = mysnprintf(sz + at, sizeof(sz) - at, "%s%s %.0f%%/%.0f%%",
						(g > 0) ? " | " : "", szGroup[g],
						100.0 * (double)wc.grpRecs[g] / (double)recs,
						(grpTotal > 0) ? (100.0 * (double)wc.grpBytes[g] / (double)grpTotal) : 0.0);
					if ((w <= 0) || ((size_t)w >= sizeof(sz) - at))
						break;
					at += (size_t)w;
				}
				snap_Say("[snap] EMITTED groups (%% of records / %% of group bytes): %s\n", sz);
				snap_Say("[snap] EMITTED: %llu records, %.1f B mean"
					" | header %.0f%% of bytes | APPEAR-only %.0f%%"
					" | enters %.1f%%\n",
					(unsigned long long)recs, (double)bytes / (double)recs,
					100.0 * (double)wc.headerBytes / (double)bytes,
					100.0 * (double)appOnly / (double)recs,
					100.0 * (double)wc.enters / (double)recs);

				char szt[320];
				at = 0; szt[0] = '\0';
				for (unsigned int t = 0; t < SNAPCL_TIERS_REPORT; ++t)
				{
					if (wc.recs[t] == 0)
						continue;
					const int w = mysnprintf(szt + at, sizeof(szt) - at, "%s%s %.1fB/%.0f%%ap",
						(at > 0) ? " | " : "", g_szTierName[t],
						(double)wc.bytes[t] / (double)wc.recs[t],
						100.0 * (double)wc.appOnly[t] / (double)wc.recs[t]);
					if ((w <= 0) || ((size_t)w >= sizeof(szt) - at))
						break;
					at += (size_t)w;
				}
				snap_Say("[snap] EMITTED by band (mean record / APPEAR-only share): %s\n", szt);

				if (wc.idGapN > 0)
					snap_Say("[snap] sorted-id gaps, per FRAME: mean %.0f | 1-byte varint %.0f%%"
						" 2-byte %.0f%% 3-byte %.0f%% (%llu gaps)\n",
						(double)wc.idGapSum / (double)wc.idGapN,
						100.0 * (double)wc.idGap[0] / (double)wc.idGapN,
						100.0 * (double)wc.idGap[1] / (double)wc.idGapN,
						100.0 * (double)wc.idGap[2] / (double)wc.idGapN,
						(unsigned long long)wc.idGapN);

				// The figure an encoder takes: its base restarts at every chunk
				// cut, where it pays a raw id.
				if (wc.chunkGapN > 0)
				{
					// Against the raw 2 bytes: one saved per 1-byte gap, one
					// lost per 3-byte gap, chunk openers excluded.
					const double saved = (double)wc.chunkGap[0]
						- (double)wc.chunkGap[2];
					snap_Say("[snap] sorted-id gaps, per CHUNK: mean %.0f | 1-byte varint %.0f%%"
						" 2-byte %.0f%% 3-byte %.0f%% | %llu chunk-opening raw ids"
						" | net %.0f B/frame\n",
						(double)wc.chunkGapSum / (double)wc.chunkGapN,
						100.0 * (double)wc.chunkGap[0] / (double)wc.chunkGapN,
						100.0 * (double)wc.chunkGap[1] / (double)wc.chunkGapN,
						100.0 * (double)wc.chunkGap[2] / (double)wc.chunkGapN,
						(unsigned long long)wc.chunkFirst,
						(g_priorityFrames > 0) ? (saved / (double)g_priorityFrames) : 0.0);
				}

				// The JOINT over EVERY emitted APPEAR, the only figure a narrow form
				// can be sized from.
				if (wc.qsAppear > 0)
					snap_Say("[snap] APPEAR narrow form, JOINT over all %llu emitted:"
						" 8-bit offset %.2f%% | 7-bit offset %.2f%%"
						" | unresolvable %.2f%% (always wide) | %u client(s)\n",
						(unsigned long long)wc.qsAppear,
						100.0 * (double)wc.qsNarrow8 / (double)wc.qsAppear,
						100.0 * (double)wc.qsNarrow7 / (double)wc.qsAppear,
						100.0 * (double)wc.qsUnresolved / (double)wc.qsAppear,
						health.activeClients);
			}
		}
		}

		memset(g_tierAccum, 0, sizeof(g_tierAccum));
		g_forcedAccum = 0;
		g_cosmeticAccum = 0;
		g_deferredAccum = 0;
		g_priorityFrames = 0;
		g_sentBytes = 0;
		g_sentPackets = 0;
		g_wirePayloadBytes = 0;
		g_wireSentBytes = 0;
		g_wirePackets = 0;
		g_wireZstdPackets = 0;
		g_wirePeak = 0;
		g_chunkDatagrams = 0;
		g_chunkFrames = 0;
		g_freshAccum = 0;
		g_resendAccum = 0;
		g_resendByteAccum = 0;
		memset(g_tierSizeAccum, 0, sizeof(g_tierSizeAccum));
		g_resAllowAccum = g_resSpentAccum = 0;
		g_resCappedAccum = g_resDrainedAccum = 0;
		g_droppedPackets = 0;
		g_heldPackets = 0;
		if (g_enterByClass != NULL)
			memset(g_enterByClass, 0, SNAP_MAXID * sizeof(unsigned int));
		g_enterClassTotal = 0;
		ResetWindowStats();

		g_outboundAtWindowStart = SERVER_STATISTIC_GetTotalOutboundDataTransferred();
		g_haveOutboundMark = true;

		g_huntingAccum = 0;
		g_liveAccum = 0;
		g_ownedAccum = 0;
		g_huntingFrames = 0;
	}

	if (g_recordFile != NULL)
	{
		snap_Say("[snap] recording: %u records, %.1f MB\n",
			g_recordRecords, (double)g_recordBytes / (1024.0 * 1024.0));
		fflush(g_recordFile);	// bound data loss to one report window
	}

	g_stats.Reset();
	g_frameClamps = 0;
}

//*****************************************************************************
//	THE DRAIN

// One queued line a pump, after the report's own timing, at the level it was queued
// with: printing the window whole would take longer than a tic.
void zx::netsnapshot::snap_DrainOneLine()
{
	if (g_reportQueue.Size() > 0)
	{
		const tictime_raw_t drainStart = TIC_GetRawCounter();
		Printf(g_reportQueue[0].printLevel, "%s", g_reportQueue[0].text.GetChars());
		g_reportQueue.Delete(0);
		const QWORD drainRaw = (QWORD)(TIC_GetRawCounter() - drainStart);
		g_drainRaw += drainRaw;
		if (drainRaw > g_drainWorstRaw)
			g_drainWorstRaw = drainRaw;
	}
}
