//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: localpred.h
//
// Description: The local player's block against the prediction it replaces:
// the shadow ring the sink stores, the comparison at the slot the replay is
// about to read, the reconciliation error and correction arrival censuses, and
// their report. Every entry is one call from cl_pred.cpp or cl_main.cpp; the
// census counts only when fua_csnap_predcensus asks, while the shadow, the slot
// stamps and the legacy marks are kept regardless.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_LOCALPRED_H__
#define __SNAP_LOCALPRED_H__

#include "doomtype.h"

namespace zx { namespace netsnapshot {

// Installs the sink the apply layer writes a local block through; with none
// installed the apply layer counts g_localNoSink and draws nothing.
void LocalPredInstall();

// Cleared with the prediction ring beside it, so a slot's stamp and its shadow
// never outlive the ring they describe.
void LocalPredReset();

// The legacy command has just written this slot, which the comparison needs to
// tell a block that found no legacy write from one that disagreed with it.
void LocalPredLegacyWrote(unsigned int tick);

// The reconciliation error: where the server's position landed against what the
// ring predicted for the same tick, both in map units as the ring holds them.
void LocalPredError(unsigned int tick, const fixed_t landed[3], const fixed_t base[3]);

// A periodic correction arrived, called by the two senders that know one is, so
// that the stock setter keeps its own signature.
void LocalPredArrival();

// The tic the replay is about to read, so every tic the cursor names lands in one
// named outcome; bIdle says prediction will not run this tic.
void LocalPredVisit(unsigned int baseTick, bool bIdle, const fixed_t pos[3],
	const fixed_t vel[3], int jumpTics);

// The replay's depth for this tic, and the report when its interval is up.
void LocalPredSample(unsigned int predictionTics);

// This slot now holds this tick, so a slot reused 70 tics later is not read as
// the tick it once held.
void LocalPredStamp(unsigned int tick);

}} // namespace zx::netsnapshot

#endif // __SNAP_LOCALPRED_H__
