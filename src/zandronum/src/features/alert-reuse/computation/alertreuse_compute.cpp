// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/alert-reuse/computation/alertreuse_compute.h"

namespace
{
// [Doruk-Aksoy] The memo for target and splash, or NULL, const as reuse is.
template <class Reuse>
auto Find(Reuse &reuse, const void *target, bool splash) -> decltype(&reuse.memos[0])
{
	for (auto &memo : reuse.memos)
	{
		if (memo.target == target && memo.splash == splash) return &memo;
	}
	return nullptr;
}
}

bool ComputeAlertRepeats(const ZxAlertReuse &reuse, const void *target, bool splash, bool hasMaxDist, int originSector, int now, int reuseTics)
{
	if (reuseTics <= 0 || hasMaxDist || originSector < 0) return false;
	const ZxAlertMemo *memo = Find(reuse, target, splash);
	if (memo == nullptr) return false;
	const int age = now - memo->tic;
	if (age < 0 || age > reuseTics) return false;
	return originSector < (int)memo->covered.size() && memo->covered[originSector] == memo->stamp;
}

ZxAlertMemo &ComputeAlertBegin(ZxAlertReuse &reuse, const void *target, bool splash, int now, int numSectors)
{
	ZxAlertMemo *memo = Find(reuse, target, splash);
	if (memo == nullptr)
	{
		if ((int)reuse.memos.size() < kZxAlertMemos)
		{
			reuse.memos.push_back(ZxAlertMemo{ target, splash, 0, 0, {} });
			memo = &reuse.memos.back();
		}
		else
		{
			memo = &reuse.memos[0];
			for (ZxAlertMemo &other : reuse.memos)
			{
				if (other.tic < memo->tic) memo = &other;
			}
			memo->target = target;
			memo->splash = splash;
		}
	}
	if ((int)memo->covered.size() != numSectors) memo->covered.assign(numSectors, 0);
	memo->tic = now;
	memo->stamp = reuse.nextStamp++;
	return *memo;
}

void ComputeAlertCovers(ZxAlertMemo &memo, int sector)
{
	if (sector >= 0 && sector < (int)memo.covered.size()) memo.covered[sector] = memo.stamp;
}
