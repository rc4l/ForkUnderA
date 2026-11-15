//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: fuzz.cpp
//
// Description: fua_snap_fuzz, the decoder fuzzer in the engine: mutated
// payloads through the real ApplyDelta and cosmetic event streams
// through the real CosmeticEvents, against a live ghost table.
//
//-----------------------------------------------------------------------------

#include <string.h>
#include <stdlib.h>

#include "doomdef.h"
#include "r_state.h"
#include "p_local.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "network.h"
#include "networkshared.h"
#include "features/net-snapshot/entities.h"
#include "features/net-snapshot/ghostactor.h"
#include "features/net-snapshot/movers.h"
#include "features/net-snapshot/fuzzgen.h"

using namespace zx::netsnapshot;

// Inputs between table resets: enough for records to meet ghosts that earlier
// inputs entered, few enough that the table never fills.
#define FUZZ_RESET_EVERY	1024

// Counts the [snap] report may skip: cleared before the run, so the summary
// counts only it, and after the report, so none reach the next one.
static void fuzz_ClearCounts()
{
	unsigned int a, b, c;
	ClientMoverStats(a, b, c, true);
	ClientLocalStats(a, b, true);
	ClientPSpriteStats(a, b, true);
	CosmeticCountsReset();
}

//   fua_snap_fuzz [inputs [seed]] [sinks|spawn]
// `sinks` sends mover records to the mover sink and `spawn` makes the cosmetic
// events' actors, both changing the loaded level (test sessions only).
CCMD(fua_snap_fuzz)
{
	// It clears the ghost table, which a connection or a demo is using.
	if (NETWORK_InClientMode())
	{
		Printf("fua_snap_fuzz: disconnect or stop the demo first; it clears the "
			"ghost table\n");
		return;
	}
	// On a server, RCON reaches it and its sinks and spawns would reach clients.
	if (NETWORK_GetState() == NETSTATE_SERVER)
	{
		Printf("fua_snap_fuzz: not on a server\n");
		return;
	}

	// The mode by name anywhere, the count then the seed as numbers, and
	// anything else refused, so a typo is not run as a count of 0.
	unsigned long inputs = 200000ul;
	DWORD seed = 0x9E3779B9u;
	const char* pszMode = "";
	unsigned int numbers = 0;
	bool bArgsOk = true;
	for (int a = 1; a < argv.argc(); ++a)
	{
		const char* psz = argv[a];
		char* pEnd = NULL;
		const unsigned long v = strtoul(psz, &pEnd, 0);
		if ((stricmp(psz, "sinks") == 0) || (stricmp(psz, "spawn") == 0))
		{
			bArgsOk = bArgsOk && (pszMode[0] == '\0');
			pszMode = psz;
		}
		else if ((psz[0] >= '0') && (psz[0] <= '9') && (*pEnd == '\0') && (numbers < 2))
		{
			if (numbers++ == 0)
				inputs = v;
			else
				seed = (DWORD)v;
		}
		else
			bArgsOk = false;
	}
	SnapFuzzRng rng = { seed };
	const bool bSinks = (stricmp(pszMode, "sinks") == 0);
	const bool bSpawn = (stricmp(pszMode, "spawn") == 0);
	if ((bArgsOk == false) || (seed == 0))
	{
		Printf("Usage: fua_snap_fuzz [inputs [seed, not 0]] [sinks|spawn]\n");
		return;
	}
	if ((bSinks || bSpawn) && (gamestate != GS_LEVEL))
	{
		Printf("fua_snap_fuzz: %s needs a loaded level\n", pszMode);
		return;
	}
	if (bSinks || bSpawn)
		Printf("fua_snap_fuzz: WARNING: %s changes the loaded level; test sessions only\n", pszMode);

	// No hooks, as in the replay, so only the ghost table changes, and `sinks`
	// leaves the mover sink installed, as the connect that relies on it needs.
	const ClentLawGravityFn savedLaw = SetLawGravity(NULL);
	const ClentAnimatesFn savedAnimates = SetAnimatesLookup(NULL);
	const ClentMoverFn savedMover = bSinks ? NULL : SetMoverSink(NULL);
	const ClentLocalFn savedLocal = SetLocalSink(NULL);
	const ClentPSpriteFn savedPSprite = SetPSpriteSink(NULL);
	if (bSinks)
		MoverInstallSinks();

	// Polyobject records wait for a server's legacy-off gate, so sinks opens it
	// if shut; shutting it after drops what the records made.
	SnapMoverGateStats gate;
	ClientMoverGateStats(gate, false);
	const bool bOpenGate = bSinks && (gate.bGate == false);
	if (bOpenGate)
		MoverSetServerGate(true, 0);

	// Cosmetic streams carry a stamp other than the joined map's, so they parse
	// without spawning, unless `spawn` joins stamp 0 for the run.
	BYTE savedStamp = 0;
	const bool bWasJoined = JoinedStamp(savedStamp);
	BYTE stamp = 0;
	if (bSpawn)
		SetJoinedStamp(true, stamp);
	else
	{
		while (MapStampCurrent(stamp))
			++stamp;
	}

	ClientReset();
	GhostClearAll();

	// sinks draws mover indices within the level, so records reach the sink's
	// apply code rather than its range check; other modes draw any index.
	SnapFuzzLevel lv = { 65536, 65536 };
	if (bSinks)
	{
		lv.sectors = (unsigned int)numsectors;
		lv.polys = (unsigned int)po_NumPolyobjs;
	}
	// Every other pair of seeds is written in a frame with wide axes, which
	// cycle; each input is read in its seed's frame.
	std::vector<SnapFuzzSeed> pool;
	for (unsigned int k = 0; k < 64; ++k)
	{
		const unsigned int bits = ((k >> 1) & 1) ? (1 + (k >> 2) % 7) : 0;
		pool.push_back((k & 1) ? FuzzSeedCosmetic(rng, bits)
			: FuzzSeedPayload(rng, bits, lv));
	}
	const SnapFrame savedFrame = ClientMapFrame();

	fuzz_ClearCounts();

	unsigned long payloads = 0, withErrors = 0, streams = 0;
	unsigned long enters = 0, dirty = 0, leaves = 0, redundant = 0;
	char errText[128], firstErr[128];
	firstErr[0] = '\0';
	int tic = 100;
	unsigned int wallMS = 1000;
	for (unsigned long i = 0; i < inputs; ++i)
	{
		if ((i % FUZZ_RESET_EVERY) == (FUZZ_RESET_EVERY - 1))
			ClientReset();

		SnapFuzzSeed s;
		bool bCosmetic;
		if ((i % 16) == 15)
		{
			s.bytes.resize(1 + FuzzRand(rng) % 200);	// raw random bytes
			FuzzFill(rng, &s.bytes[0], s.bytes.size());
			bCosmetic = (FuzzRand(rng) & 1) != 0;
		}
		else
		{
			const unsigned int k = FuzzRand(rng) % (unsigned int)pool.size();
			s = pool[k];
			bCosmetic = (k & 1) != 0;
			FuzzMutate(rng, s);
			if (s.bytes.empty())
				continue;
		}

		UseMapFrame(FuzzFrame(s.frameBits));
		if (bCosmetic)
		{
			s.bytes[0] = stamp;
			BYTESTREAM_s stream;
			stream.pbStream = &s.bytes[0];
			stream.pbStreamEnd = stream.pbStream + s.bytes.size();
			CosmeticEvents(&stream);
			++streams;
			continue;
		}

		// Half read as a pre-v28 stream (no gen byte in the mover header), and
		// one in eight late, as a reordered datagram is.
		SetStreamVersion((FuzzRand(rng) & 8) ? 27u : (unsigned int)ZSNAP_VERSION);
		const int at = ((FuzzRand(rng) & 7) == 0)
			? tic - 1 - (int)(FuzzRand(rng) % 4) : tic++;
		wallMS += 28;
		SnapApplyStats stats;
		const unsigned int errs = ApplyDelta(&s.bytes[0], (unsigned int)s.bytes.size(),
			at, at, wallMS, errText, sizeof(errText), &stats);
		++payloads;
		if ((errs != 0) && (withErrors++ == 0))
			strcpy(firstErr, errText);
		enters += stats.enters;
		dirty += stats.dirty;
		leaves += stats.leaves;
		redundant += stats.redundant;
	}

	const unsigned int present = PresentCount();
	unsigned int movers, noSink, bad;
	ClientMoverStats(movers, noSink, bad, false);

	if (bOpenGate)
		MoverSetServerGate(false, 0);
	ClientReset();
	GhostClearAll();
	UseMapFrame(savedFrame);
	SetJoinedStamp(bWasJoined, savedStamp);
	SetLawGravity(savedLaw);
	SetAnimatesLookup(savedAnimates);
	if (bSinks == false)
		SetMoverSink(savedMover);
	SetLocalSink(savedLocal);
	SetPSpriteSink(savedPSprite);

	Printf("fua_snap_fuzz: done (%lu inputs, seed 0x%08x%s%s)\n", inputs, (unsigned int)seed,
		(pszMode[0] != '\0') ? ", " : "", pszMode);
	Printf("  payloads %lu: %lu with errors | enters %lu, dirty %lu, leaves %lu, "
		"redundant %lu | %u present before the reset\n",
		payloads, withErrors, enters, dirty, leaves, redundant, present);
	if (withErrors != 0)
		Printf("  first error: %s\n", firstErr);
	Printf("  movers: %u applied, %u with no sink, %u unreadable | cosmetic streams %lu\n",
		movers, noSink, bad, streams);

	// The run's counts would read as failures in the next [snap] report (DIVERGED,
	// ABANDONED, no sink), so that report prints now and clears them.
	Printf("fua_snap_fuzz: the [snap] report below is the run's\n");
	GhostReportNow();
	fuzz_ClearCounts();
}
