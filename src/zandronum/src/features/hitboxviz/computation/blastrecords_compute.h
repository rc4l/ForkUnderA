// [MGOOOOOO] Bounded store of recent explosion damage regions for the debug overlay. An explosion
// is an event, not an object -- P_RadiusAttack runs and returns within a single tic -- so there is
// no actor to hang the visualization on and the regions have to be remembered for a moment after
// the fact. Deliberately engine-free (no AActor, no gametic global) so it unit-tests standalone.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_BLASTRECORDS_COMPUTE_H
#define ZX_HITBOXVIZ_BLASTRECORDS_COMPUTE_H

#include "features/hitboxviz/computation/recordstore_compute.h"

namespace zx { namespace hitboxviz {

struct BlastRecord
{
	float x, y, z;
	int distance;
	int fulldamagedistance;
	// Tic at which this record stops being drawn. It is expired once the current tic reaches it.
	int expiryTic;
};

enum
{
	// A rocket-heavy firefight can produce a lot of blasts in one second; beyond this many the
	// overlay is unreadable anyway, so the oldest are dropped rather than growing without bound.
	MAX_BLAST_RECORDS = 64,
	// How long a region stays on screen, in tics (~1 second at 35Hz).
	BLAST_RECORD_LIFETIME_TICS = 35,
};

// [MGOOOOOO] The blasts' store, the shared one (recordstore_compute.h) holding MAX_BLAST_RECORDS.
typedef RecordStore<BlastRecord, MAX_BLAST_RECORDS> BlastRecordStore;

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_BLASTRECORDS_COMPUTE_H
