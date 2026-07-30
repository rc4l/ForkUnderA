// [MGOOOOOO] See blastrecords_compute.h.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#include "features/hitboxviz/computation/blastrecords_compute.h"

namespace zx { namespace hitboxviz {

BlastRecordStore::BlastRecordStore() : mCount(0)
{
}

void BlastRecordStore::Push(const BlastRecord &record)
{
	if (mCount == MAX_BLAST_RECORDS)
	{
		// Full: drop the oldest and shift down. The capacity is small and this only runs once per
		// explosion, so the shift is cheaper than the bookkeeping a ring buffer would need to keep
		// oldest-first iteration working across a wrap.
		for (unsigned int i = 1; i < MAX_BLAST_RECORDS; ++i)
			mRecords[i - 1] = mRecords[i];

		mCount = MAX_BLAST_RECORDS - 1;
	}

	mRecords[mCount] = record;
	++mCount;
}

void BlastRecordStore::ExpireBefore(int nowTic)
{
	unsigned int kept = 0;

	for (unsigned int i = 0; i < mCount; ++i)
	{
		if (mRecords[i].expiryTic > nowTic)
		{
			if (kept != i)
				mRecords[kept] = mRecords[i];
			++kept;
		}
	}

	mCount = kept;
}

void BlastRecordStore::Clear()
{
	mCount = 0;
}

}} // namespace zx::hitboxviz
