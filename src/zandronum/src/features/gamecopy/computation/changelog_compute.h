// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Which items of the map changed since the copy last took them, noted where they change, so a tic's end compares those instead of the whole map.
#ifndef ZX_CHANGELOG_COMPUTE_H
#define ZX_CHANGELOG_COMPUTE_H
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
namespace zx { namespace gamecopy {
// [Doruk-Aksoy] Each item noted once, in the order noted; `whole` when a note fell outside the items, so only a whole-map compare is safe.
struct ChangeLog
{
	std::vector<unsigned char> noted;
	std::vector<int> list;
	bool whole = true;
};
// [Doruk-Aksoy] A log over count items with nothing noted, or one that must be compared whole.
void ComputeLogReset(ChangeLog &log, int count, bool whole);
// [Doruk-Aksoy] Item index noted, once.
void ComputeLogNote(ChangeLog &log, int index);
// [Doruk-Aksoy] Nothing noted any more, once what was noted has been taken.
void ComputeLogClear(ChangeLog &log);
// [Doruk-Aksoy] Only the noted items keep says to keep, in their order.
void ComputeLogKeepIf(ChangeLog &log, const std::function<bool(int)> &keep);
// [Doruk-Aksoy] Which of count items of size bytes from first the address at is, or -1 when it is none of them.
int ComputeIndexIn(uintptr_t at, uintptr_t first, size_t size, int count);
// [Doruk-Aksoy] The next size items of count to compare whole, from cursor, wrapping to the start once it reaches the end.
void ComputeVerifySlice(int count, int size, int &cursor, int &lo, int &hi);
}} // namespace zx::gamecopy
#endif // ZX_CHANGELOG_COMPUTE_H
