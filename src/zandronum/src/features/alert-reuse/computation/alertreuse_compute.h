// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

// [Doruk-Aksoy] Pure rules for skipping a noise alert that repeats one a few tics old from inside the area that one already reached.
#ifndef ZX_ALERTREUSE_COMPUTE_H
#define ZX_ALERTREUSE_COMPUTE_H

#include <vector>

// [Doruk-Aksoy] The last full alert for one target and splash: its tic, its stamp, and each sector's stamp when that alert reached it without crossing a sound-blocking line.
struct ZxAlertMemo
{
	const void *target;
	bool splash;
	int tic;
	int stamp;
	std::vector<int> covered;
};

// [Doruk-Aksoy] Every target's last full alert, at most kZxAlertMemos of them, and the stamp the next full alert takes.
struct ZxAlertReuse
{
	std::vector<ZxAlertMemo> memos;
	int nextStamp = 1;
};

constexpr int kZxAlertMemos = 16;

// [Doruk-Aksoy] Whether an alert can be skipped: the target's last full one is at most reuseTics old and reached origin unblocked, and this alert has no maximum distance, which reaches around its own emitter.
bool ComputeAlertRepeats(const ZxAlertReuse &reuse, const void *target, bool splash, bool hasMaxDist, int originSector, int now, int reuseTics);

// [Doruk-Aksoy] Starts a full alert's memo for target and splash at now over numSectors sectors, replacing the oldest memo when all are taken, and returns it to mark sectors on.
ZxAlertMemo &ComputeAlertBegin(ZxAlertReuse &reuse, const void *target, bool splash, int now, int numSectors);

// [Doruk-Aksoy] Forgets every memo, as a new level must, since its clock restarts and a target may take a gone one's address.
void ComputeAlertForget(ZxAlertReuse &reuse);

// [Doruk-Aksoy] Marks a sector the full alert reached without crossing a sound-blocking line.
void ComputeAlertCovers(ZxAlertMemo &memo, int sector);

#endif
