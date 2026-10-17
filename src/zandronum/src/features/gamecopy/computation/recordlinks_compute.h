// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Lists of records held as links by index, as the copy keeps each sector's actors: a list's first record, each record's next, -1 ending them.

#ifndef ZX_RECORDLINKS_COMPUTE_H
#define ZX_RECORDLINKS_COMPUTE_H

namespace zx { namespace gamecopy {

// [rc4l] Builds one list at a time in the order records are added.
class RecordLinks
{
public:
	// [rc4l] Starts a list, empty until a record is added, whose first record goes in first.
	void Begin(int &first) { first = -1; m_tail = &first; }
	// [rc4l] Appends record r, whose own link to the record after it is next.
	void Add(int r, int &next) { *m_tail = r; next = -1; m_tail = &next; }

private:
	int *m_tail = nullptr;
};

// [rc4l] Whether r names one of count records.
inline bool ComputeHoldsRecord(int r, int count) { return r >= 0 && r < count; }

}} // namespace zx::gamecopy

#endif // ZX_RECORDLINKS_COMPUTE_H
