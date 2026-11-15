//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: record.cpp
//
// Description: The client-side snapshot recorder (fua_csnap_record,
// fua_csnap_stoprecord). The .csnap reader is cl_recordread.cpp.
//
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "g_level.h"
#include "p_setup.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "i_system.h"
#include "zstring.h"
#include "tarray.h"
#include "features/net-snapshot/record.h"
#include "features/net-snapshot/entities.h"

using namespace zx::netsnapshot;

//*****************************************************************************
//	THE CVAR SET
//
// Every cvar the sampler's output depends on, recorded by name.
static const char* const g_csnapCvarNames[] =
{
	"fua_csnap_rendercull",
	"fua_csnap_localanim",
	"fua_csnap_cull_near",
	"fua_csnap_cull_far",
	// fua_csnap_missile_extrap caps ballistic extrapolation and is
	// CVAR_ARCHIVE, so it differs between machines without anyone touching it.
	"fua_csnap_missile_extrap",
	// Aging by the clock model, and when it falls back to arrival.
	"fua_csnap_age_servertime",
	"fua_csnap_age_maxjitter",
	// The replay draws through the mirror's smoothing too.
	"fua_csnap_smooth_step",
	"fua_csnap_smooth_nonballistic",
	"fua_csnap_smooth_ballistic",
	"fua_csnap_smooth_entities",
	// Stock's switches for how a held ghost the smoothing leaves alone is drawn:
	// slid over its tic, or stepped as it lands.
	"nomonsterinterpolation",
	"cl_capfps",
	// Not a sampler setting: it says which fraction of drawn-state changes the
	// file carries, so a reader can tell a stride from a quiet map.
	"fua_csnap_recmirror",
	NULL
};

static unsigned int clrec_CvarCount()
{
	unsigned int n = 0;
	while (g_csnapCvarNames[n] != NULL)
		++n;
	return n;
}

// Read one by name through the cvar registry rather than by linking each
// symbol: this file then has no opinion about where they are defined, and a
// renamed or removed cvar shows up as a recorded value of zero with its name
// intact rather than as a build break in an unrelated file.
static int clrec_CvarValue(const char* pszName)
{
	FBaseCVar* pVar = FindCVar(pszName, NULL);
	if (pVar == NULL)
		return 0;
	return pVar->GetGenericRep(CVAR_Int).Int;
}

//*****************************************************************************
//	RECORDING

static FILE*			g_recFile = NULL;
static unsigned int		g_recRecords = 0;
static QWORD			g_recBytes = 0;
static int*				g_recCvarLast = NULL;	// last written values
static bool				g_recCvarDirty = false;

static void clrec_Write8(BYTE v) { fwrite(&v, 1, 1, g_recFile); g_recBytes += 1; }
static void clrec_Write16(WORD v) { fwrite(&v, 2, 1, g_recFile); g_recBytes += 2; }
static void clrec_Write32(DWORD v) { fwrite(&v, 4, 1, g_recFile); g_recBytes += 4; }
static void clrec_Write64(QWORD v) { fwrite(&v, 8, 1, g_recFile); g_recBytes += 8; }

bool zx::netsnapshot::RecorderActive()
{
	return (g_recFile != NULL);
}

// Writes the cvar block: count, then (nameLen, name, value) triples.
static void clrec_WriteCvarBlock()
{
	const unsigned int n = clrec_CvarCount();
	clrec_Write16((WORD)n);
	for (unsigned int i = 0; i < n; ++i)
	{
		const char* pszName = g_csnapCvarNames[i];
		const size_t len = strlen(pszName);
		clrec_Write8((BYTE)len);
		fwrite(pszName, 1, len, g_recFile);
		g_recBytes += len;
		const int v = clrec_CvarValue(pszName);
		clrec_Write32((DWORD)v);
		if (g_recCvarLast != NULL)
			g_recCvarLast[i] = v;
	}
	g_recCvarDirty = false;
}

// Has anything in the set moved since it was last written?
static bool clrec_CvarsChanged()
{
	if (g_recCvarLast == NULL)
		return false;
	const unsigned int n = clrec_CvarCount();
	for (unsigned int i = 0; i < n; ++i)
	{
		if (clrec_CvarValue(g_csnapCvarNames[i]) != g_recCvarLast[i])
			return true;
	}
	return false;
}

void zx::netsnapshot::RecorderStopIfActive(const char* pszReason)
{
	if (g_recFile == NULL)
		return;

	fclose(g_recFile);
	g_recFile = NULL;
	delete[] g_recCvarLast;
	g_recCvarLast = NULL;
	Printf("fua_csnap_record: stopped (%s). %u records, %.1f MB.\n",
		pszReason, g_recRecords, (double)g_recBytes / (1024.0 * 1024.0));
}

// Drawn state, on change, timed by the CSNAP_FRAME before it.
void zx::netsnapshot::RecorderMirror(unsigned int id, WORD qstate, WORD sprite, BYTE frame, BYTE tics)
{
	if (g_recFile == NULL)
		return;

	clrec_Write8((BYTE)CSNAP_MIRROR);
	clrec_Write16((WORD)id);
	clrec_Write16(qstate);
	clrec_Write16(sprite);
	clrec_Write8(frame);
	clrec_Write8(tics);
}

void zx::netsnapshot::RecorderDelta(const BYTE* pData, unsigned int size, int serverTic, int gameTic,
	unsigned int arrivalMS)
{
	if ((g_recFile == NULL) || (pData == NULL))
		return;

	// Stored raw, as the payload arrived decompressed.
	clrec_Write8((BYTE)CSNAP_DELTA);
	clrec_Write32((DWORD)serverTic);
	clrec_Write32((DWORD)gameTic);
	clrec_Write32((DWORD)arrivalMS);
	clrec_Write32(size);
	if (size > 0)
	{
		if (fwrite(pData, 1, size, g_recFile) != size)
		{
			RecorderStopIfActive("write failed");
			return;
		}
		g_recBytes += size;
	}
	++g_recRecords;
}

void zx::netsnapshot::RecorderFrame(unsigned int renderMS, const CsnapPlayerPose& pose)
{
	if (g_recFile == NULL)
		return;

	// A cvar change rides the frame that first sees it, so the replay applies
	// it at the same point.
	const bool changed = clrec_CvarsChanged();

	clrec_Write8((BYTE)CSNAP_FRAME);
	clrec_Write32((DWORD)renderMS);
	// 64-bit, so the pose holds a fixed_t wider than 3.3's.
	clrec_Write64((QWORD)(SQWORD)pose.x);
	clrec_Write64((QWORD)(SQWORD)pose.y);
	clrec_Write64((QWORD)(SQWORD)pose.z);
	clrec_Write32((DWORD)pose.angle);
	clrec_Write32((DWORD)pose.pitch);
	clrec_Write8(pose.valid ? 1 : 0);
	clrec_Write8(changed ? 1 : 0);
	if (changed)
		clrec_WriteCvarBlock();
	++g_recRecords;
}

void zx::netsnapshot::RecorderReset(const char* pszReason)
{
	if (g_recFile == NULL)
		return;

	clrec_Write8((BYTE)CSNAP_RESET);
	clrec_Write32((DWORD)I_MSTime());
	const size_t len = (pszReason != NULL) ? strlen(pszReason) : 0;
	clrec_Write8((BYTE)((len > 63) ? 63 : len));
	if (len > 0)
	{
		const size_t w = (len > 63) ? 63 : len;
		fwrite(pszReason, 1, w, g_recFile);
		g_recBytes += w;
	}
	++g_recRecords;
}

bool zx::netsnapshot::RecorderLevelChecksum(BYTE checksum[16])
{
	if (gamestate != GS_LEVEL)
		return false;
	// [Kaori-Chani] FLevelLocals keeps the lump name as an FString here, not a char array.
	// It converts to const char* on its own, so the call reads the same.
	MapData* pMap = P_OpenMapData(level.MapName, false);
	if (pMap == NULL)
		return false;
	pMap->GetChecksum(checksum);
	delete pMap;
	return true;
}

// Kind, name length (up to 63), name, the 16-byte checksum, then the map's
// frame: three 32-bit origins and three widths.
static void clrec_WriteMap()
{
	BYTE checksum[16];
	if (RecorderLevelChecksum(checksum) == false)
		return;

	const size_t len = level.MapName.Len();
	const size_t w = (len > 63) ? 63 : len;
	clrec_Write8((BYTE)CSNAP_MAP);
	clrec_Write8((BYTE)w);
	fwrite(level.MapName.GetChars(), 1, w, g_recFile);
	fwrite(checksum, 1, 16, g_recFile);
	g_recBytes += w + 16;
	const SnapFrame &frame = ClientMapFrame();
	for (unsigned int k = 0; k < 3; ++k)
		clrec_Write32((DWORD)frame.origin[k]);
	for (unsigned int k = 0; k < 3; ++k)
		clrec_Write8(frame.width[k]);
	++g_recRecords;
}

void zx::netsnapshot::RecorderMapJoined()
{
	if (g_recFile != NULL)
		clrec_WriteMap();
}

CCMD(fua_csnap_record)
{
	if (argv.argc() < 2)
	{
		Printf("usage: fua_csnap_record <name>\n");
		return;
	}

	RecorderStopIfActive("restarted");

	FString path;
	path.Format("%s.csnap", argv[1]);
	g_recFile = fopen(path.GetChars(), "wb");
	if (g_recFile == NULL)
	{
		Printf("fua_csnap_record: cannot open %s\n", path.GetChars());
		return;
	}

	g_recRecords = 0;
	g_recBytes = 0;
	g_recCvarLast = new int[clrec_CvarCount()];
	memset(g_recCvarLast, 0, clrec_CvarCount() * sizeof(int));

	fwrite(CSNAP_MAGIC, 1, 4, g_recFile);
	g_recBytes += 4;
	clrec_Write16((WORD)CSNAP_VERSION);
	clrec_Write16((WORD)TICRATE);
	clrec_Write32(0);					// flags: payloads stored raw
	// Not derivable from the stream: the pose is `pCam`, which under a chasecam
	// is the SPIED player rather than this one.
	clrec_Write32((DWORD)consoleplayer);
	clrec_WriteCvarBlock();

	// The map ahead of the baseline, or nothing outside a level until SnapJoin.
	clrec_WriteMap();

	// A self-contained baseline first, as the server's recorder writes one.
	{
		// Sized for the worst case, as a truncated baseline would look fine.
		static TArray<BYTE> s_baseline;
		const unsigned int need = SNAP_MAXID * SNAP_MAX_RECORD_BYTES;
		if (s_baseline.Size() < need)
			s_baseline.Resize(need);

		int baseTic = 0;
		const unsigned int present = PresentCount();
		const unsigned int n = ExportBaseline(&s_baseline[0], s_baseline.Size(), baseTic);
		if (n > 0)
		{
			// Stamped on the deltas' clock, the newest game tic held.
			RecorderDelta(&s_baseline[0], n, baseTic, NewestGameTic(),
				I_MSTime());
			Printf("fua_csnap_record: baseline %u bytes, %u entities\n", n, present);
		}
		else
		{
			// Not fatal, but the replay will see only what enters during the
			// recording.
			Printf("fua_csnap_record: WARNING no ghosts present, stream will not be "
				"self-contained\n");
		}
	}

	Printf("fua_csnap_record: recording to %s\n", path.GetChars());
}

CCMD(fua_csnap_stoprecord)
{
	if (RecorderActive() == false)
	{
		Printf("fua_csnap_record: not recording\n");
		return;
	}
	RecorderStopIfActive("user");
}
