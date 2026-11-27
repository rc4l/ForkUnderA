// [MGOOOOOO] A bounded store of the overlay's events, things over within a tic (an explosion, an actor spawned and gone in one tic) that its frames still have to draw.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO
#ifndef ZX_HITBOXVIZ_RECORDSTORE_COMPUTE_H
#define ZX_HITBOXVIZ_RECORDSTORE_COMPUTE_H

namespace zx { namespace hitboxviz {

// [MGOOOOOO] Insertion-ordered and fixed-capacity, oldest first, the oldest evicted when full; Record needs an int expiryTic.
template <typename Record, unsigned int Capacity>
class RecordStore
{
public:
	RecordStore() : mCount(0) {}

	// [MGOOOOOO] Appends a record, shifting the oldest out when full (cheaper at this size than a ring buffer's oldest-first bookkeeping).
	void Push(const Record &record)
	{
		if (mCount == Capacity)
		{
			for (unsigned int i = 1; i < Capacity; ++i)
				mRecords[i - 1] = mRecords[i];

			mCount = Capacity - 1;
		}

		mRecords[mCount] = record;
		++mCount;
	}

	// [MGOOOOOO] Drops every record whose expiryTic has been reached (expiryTic <= nowTic), compacting since records are in no expiry order.
	void ExpireBefore(int nowTic)
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

	// [MGOOOOOO] Drops everything, on a map change, reconnect or demo seek, where records would be drawn on another level.
	void Clear() { mCount = 0; }

	unsigned int Count() const { return mCount; }

	// [MGOOOOOO] Valid for index < Count(), index 0 the oldest surviving record.
	const Record &Get(unsigned int index) const { return mRecords[index]; }

private:
	Record mRecords[Capacity];
	unsigned int mCount;
};

}} // namespace zx::hitboxviz

#endif // ZX_HITBOXVIZ_RECORDSTORE_COMPUTE_H
