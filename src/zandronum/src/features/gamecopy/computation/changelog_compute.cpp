// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/gamecopy/computation/changelog_compute.h"

#include <algorithm>

namespace zx { namespace gamecopy {

void ComputeLogReset(ChangeLog &log, int count, bool whole)
{
	log.noted.assign(count > 0 ? count : 0, 0);
	log.list.clear();
	log.whole = whole;
}

void ComputeLogNote(ChangeLog &log, int index)
{
	if (index < 0 || index >= (int)log.noted.size())
	{
		log.whole = true;
		return;
	}
	if (log.noted[index]) return;
	log.noted[index] = 1;
	log.list.push_back(index);
}

void ComputeLogClear(ChangeLog &log)
{
	for (int i : log.list) log.noted[i] = 0;
	log.list.clear();
	log.whole = false;
}

void ComputeLogKeepIf(ChangeLog &log, const std::function<bool(int)> &keep)
{
	size_t kept = 0;
	for (int i : log.list)
	{
		if (keep(i)) log.list[kept++] = i;
		else log.noted[i] = 0;
	}
	log.list.resize(kept);
}

int ComputeIndexIn(uintptr_t at, uintptr_t first, size_t size, int count)
{
	if (first == 0 || size == 0 || count <= 0 || at < first) return -1;
	const uintptr_t offset = at - first;
	if (offset % size != 0 || offset / size >= (uintptr_t)count) return -1;
	return (int)(offset / size);
}

void ComputeVerifySlice(int count, int size, int &cursor, int &lo, int &hi)
{
	if (count <= 0 || size <= 0)
	{
		cursor = lo = hi = 0;
		return;
	}
	if (cursor < 0 || cursor >= count) cursor = 0;
	lo = cursor;
	hi = std::min(count, lo + size);
	cursor = hi >= count ? 0 : hi;
}

}} // namespace zx::gamecopy
