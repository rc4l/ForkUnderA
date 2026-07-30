// [MGOOOOOO] Bounded store of recent explosion damage regions for the debug overlay. An explosion
// is an event, not an object -- P_RadiusAttack runs and returns within a single tic -- so there is
// no actor to hang the visualization on and the regions have to be remembered for a moment after
// the fact. Deliberately engine-free (no AActor, no gametic global) so it unit-tests standalone.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_BLASTRECORDS_COMPUTE_H
#define ZX_HITBOXVIZ_BLASTRECORDS_COMPUTE_H

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

// Insertion-ordered, fixed-capacity. Oldest-first iteration; oldest evicted when full.
class BlastRecordStore
{
public:
	BlastRecordStore();

	// Appends a record, evicting the oldest if already at capacity.
	void Push(const BlastRecord &record);

	// Drops every record whose expiryTic has been reached (expiryTic <= nowTic). Records are not
	// assumed to be in expiry order, so this compacts rather than just popping the front.
	void ExpireBefore(int nowTic);

	// Drops everything -- used on map change, reconnect and demo seek, where surviving records
	// would be drawn at coordinates belonging to a different level.
	void Clear();

	unsigned int Count() const { return mCount; }

	// Valid for index < Count(); index 0 is the oldest surviving record.
	const BlastRecord &Get(unsigned int index) const { return mRecords[index]; }

private:
	BlastRecord mRecords[MAX_BLAST_RECORDS];
	unsigned int mCount;
};

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_BLASTRECORDS_COMPUTE_H
