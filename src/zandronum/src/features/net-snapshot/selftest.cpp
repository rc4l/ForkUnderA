// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: selftest.cpp
//
// Description: fua_snap_verify, which validates a .zsnap through the
// client's own decoder, and fua_snap_selftest with the block list behind its
// summary. Kept out of entities.cpp so the ghost table carries no file I/O
// or test code.
//
//-----------------------------------------------------------------------------

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "doomdef.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "network.h"
#include "features/net-snapshot/format.h"
#include "features/net-snapshot/stream.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/selftest.h"
#include "features/net-snapshot/capture.h"	// SnapFileFind, the folder it reads from

using namespace zx::netsnapshot;

int zx::netsnapshot::VerifyFixture(const char* pszFilename, double skew)
{
	SnapStreamReader reader;
	if (ZsnapOpen(reader, pszFilename) == false)
	{
		Printf("fua_snap_verify: %s ('%s').\n", reader.error, pszFilename);
		return 2;
	}

	Printf("zsnap v%u  ticrate=%u  flags=0x%x  skew=%.3f\n",
		reader.version, reader.ticrate, reader.flags, skew);

	ClientReset();
	ClockReset();
	SetStreamVersion(reader.version);
	const SnapFrame savedFrame = ClientMapFrame();
	UseMapFrame(reader.frame);

	unsigned int records = 0, emptyRecords = 0, decodeErrors = 0;
	unsigned int totalEnters = 0, totalDirty = 0, totalLeaves = 0;
	unsigned int baselineRec = 0, baselineEnters = 0;
	unsigned int wallRegressions = 0, prevWall = 0;
	int firstTic = 0, lastTic = 0;
	unsigned int firstMS = 0, lastMS = 0, rawFirstMS = 0;
	bool haveFirst = false, haveRawFirst = false, truncated = false, aborted = false;
	char errText[128];

	for (;; )
	{
		const ESnapReadResult res = ZsnapReadRecord(reader);
		if (res == SNAPREAD_END)
			break;
		if (res == SNAPREAD_TRUNCATED)
		{
			Printf("WARNING: file ends mid-record after record #%u "
				"(crash tail? the recorder flushes once per stats window)\n", records);
			truncated = true;
			break;
		}
		if (res == SNAPREAD_ERROR)
		{
			Printf("record #%u: %s - validation aborted (presence tracking "
				"cannot soundly continue).\n", reader.recordIndex, reader.error);
			++decodeErrors;
			aborted = true;
			break;
		}

		++records;
		unsigned int wall = reader.wallMS;

		// Skew is applied BEFORE any timestamp is captured, so the clock
		// estimate and the session average describe the same timeline.
		if (skew != 1.0)
		{
			if (haveRawFirst == false)
			{
				rawFirstMS = wall;
				haveRawFirst = true;
			}
			const double scaled = (double)rawFirstMS + (((double)wall - (double)rawFirstMS) * skew);
			wall = (scaled > 0.0) ? (unsigned int)scaled : 0;
		}

		if (haveFirst == false)
		{
			firstTic = reader.tic;
			firstMS = wall;
			haveFirst = true;
		}
		else if (wall < prevWall)
			++wallRegressions;

		lastTic = reader.tic;
		prevWall = wall;
		lastMS = wall;

		ClockSample(reader.tic, wall);

		if (reader.payloadSize == 0)
		{
			++emptyRecords;
			continue;
		}

		SnapApplyStats stats;
		const unsigned int errs = ApplyDelta(reader.payload, reader.payloadSize,
			reader.tic, reader.tic, wall, errText, sizeof(errText), &stats);
		if (errs != 0)
		{
			Printf("record #%u: %u error(s), first: %s\n", records, errs, errText);
			decodeErrors += errs;
		}
		// A recording never re-sends a record, so a redundant one means its
		// presence tracking has gone wrong.
		if (stats.redundant != 0)
		{
			Printf("record #%u: %u redundant record(s), an enter for an id already "
				"present or a leave for one absent\n", records, stats.redundant);
			decodeErrors += stats.redundant;
		}
		// Nor does it go back in time, so a record older than state applied
		// means its tics are out of order.
		if (stats.late != 0)
		{
			Printf("record #%u: %u late record(s), older than state already applied\n",
				records, stats.late);
			decodeErrors += stats.late;
		}
		totalEnters += stats.enters;
		totalDirty += stats.dirty;
		totalLeaves += stats.leaves;

		if (baselineRec == 0)
		{
			baselineRec = records;
			baselineEnters = stats.enters;
			if ((stats.dirty != 0) || (stats.leaves != 0))
			{
				Printf("record #%u: expected pure baseline (first non-empty record), "
					"got %u dirty / %u leave\n", records, stats.dirty, stats.leaves);
				++decodeErrors;
			}
		}
	}

	const unsigned int ticrate = reader.ticrate ? reader.ticrate : TICRATE;
	ZsnapClose(reader);

	Printf("records: %u (%u empty) | tics %d..%d | enters=%u dirty=%u leaves=%u\n",
		records, emptyRecords, firstTic, lastTic, totalEnters, totalDirty, totalLeaves);
	if (baselineRec != 0)
		Printf("baseline: %u entities entered in record #%u%s\n", baselineEnters, baselineRec,
			(baselineRec == 1) ? "" : " (leading records were empty/timing-only)");

	const double nominal = 1000.0 / (double)ticrate;

	if (ClockReady())
	{
		const double est = ClockMsPerTic();
		if (est > 0.0)
			Printf("clock: %.3f ms/tic (current regime; nominal %.3f) -> %.1f%% speed\n",
				est, nominal, 100.0 * nominal / est);
		else
			Printf("clock: degenerate estimate - timing data is not usable\n");
	}
	else
		Printf("clock: not enough samples for an estimate\n");

	if (wallRegressions != 0)
		Printf("WARNING: wall time regressed in %u record(s) - timing data is "
			"unreliable; session average suppressed.\n", wallRegressions);
	else if ((lastTic > firstTic) && (lastMS >= firstMS))
	{
		const double avg = (double)(lastMS - firstMS) / (double)(lastTic - firstTic);
		if (avg > 0.0)
			Printf("session: %.3f ms/tic average over %d tics / %.1fs wall -> %.1f%% speed\n",
				avg, lastTic - firstTic, (double)(lastMS - firstMS) / 1000.0,
				100.0 * nominal / avg);
	}

	int code;
	const char* pszVerdict;
	if (aborted)
	{
		pszVerdict = "PARTIAL (validation aborted)";
		code = (decodeErrors > 1) ? 2 : 3;
	}
	else if (decodeErrors != 0)
	{
		pszVerdict = "FAIL";
		code = 2;
	}
	else if (truncated)
	{
		pszVerdict = "PASS (file truncated mid-record)";
		code = 4;
	}
	else
	{
		pszVerdict = "PASS";
		code = 0;
	}
	Printf("stream decode: %s (%u errors) | entities present at end: %u\n",
		pszVerdict, decodeErrors, PresentCount());

	// Validation is a read-only tool: it must not leave the runtime ghost
	// table populated for whatever runs next.
	ClientReset();
	ClockReset();
	UseMapFrame(savedFrame);
	return code;
}

//*****************************************************************************
//	CONSOLE COMMANDS

DEV_MODE_CCMD(fua_snap_verify)
{
	if (argv.argc() < 2)
	{
		Printf("Usage: fua_snap_verify <file.zsnap> [skew]\n");
		return;
	}
	// Verification clears the ghost table, which a connection or a demo uses.
	if (NETWORK_InClientMode())
	{
		Printf("fua_snap_verify: disconnect or stop the demo first; verification clears "
			"the ghost table\n");
		return;
	}
	// The name is checked, then resolved: the folder this resolves to is the module's
	// own and holds the separators the argument may not.
	if (SnapFileNameBad(argv[1]))
	{
		Printf("fua_snap_verify: filename must not contain path separators.\n");
		return;
	}
	const FString found = SnapFileFind(argv[1]);
	const char* pszName = found.GetChars();
	double skew = 1.0;
	if (argv.argc() >= 3)
	{
		skew = atof(argv[2]);
		if ((skew <= 0.01) || (skew > 100.0))
		{
			Printf("fua_snap_verify: skew must be in (0.01, 100].\n");
			return;
		}
	}
	VerifyFixture(pszName, skew);
}

//*****************************************************************************
//	SELF-TESTS: fua_snap_selftest runs every block in the subsystem.

// The blocks run since the last reset, in fixed storage (a console command must
// not depend on an allocator), with a marker for any that overflow.
#define OVERFLOW_MARK	", ..."
static char g_blockNames[512];
static unsigned int g_blockCount = 0;
static bool g_blockOverflow = false;

void zx::netsnapshot::SelfTestBlocksReset()
{
	g_blockNames[0] = '\0';
	g_blockCount = 0;
	g_blockOverflow = false;
}

void zx::netsnapshot::SelfTestBlock(const char* pszName)
{
	if (pszName == NULL)
		return;
	++g_blockCount;				// counted even when the name does not fit
	if (g_blockOverflow)
		return;
	const size_t used = strlen(g_blockNames);
	const size_t need = strlen(pszName) + ((used > 0) ? 2 : 0);
	// Room for the marker is reserved, so an overflow says so.
	if ((used + need + sizeof(OVERFLOW_MARK)) > sizeof(g_blockNames))
	{
		g_blockOverflow = true;
		strcpy(g_blockNames + used, OVERFLOW_MARK);
		return;
	}
	if (used > 0)
		strcpy(g_blockNames + used, ", ");
	strcat(g_blockNames, pszName);
}

const char* zx::netsnapshot::SelfTestBlocks(unsigned int& count)
{
	count = g_blockCount;
	return g_blockNames;
}

void zx::netsnapshot::SelfTestPrint(const char* pszFormat, ...)
{
	va_list args;
	va_start(args, pszFormat);
	VPrintf(PRINT_HIGH, pszFormat, args);
	va_end(args);
}

DEV_MODE_CCMD(fua_snap_selftest)
{
	SelfTestBlocksReset();
	unsigned int failures = RunFormatSelfTests();
	failures += RunSelfTests();

	// Blocks name themselves as they run, so one skipped at runtime (the
	// ghost-table tests refuse a live table) is simply absent from the list.
	unsigned int blocks = 0;
	const char* pszBlocks = SelfTestBlocks(blocks);
	if (failures == 0)
		Printf("fua_snap_selftest: PASS (%u blocks: %s)\n", blocks, pszBlocks);
	else
		Printf("fua_snap_selftest: FAIL (%u) over %u blocks: %s\n",
			failures, blocks, pszBlocks);
}
