// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/chase-trim/computation/chasetrim_compute.h"

namespace zx { namespace chasetrim {

FarStep ComputeFarChaseStep(bool far, uint8_t &state)
{
	const int hold = state >> 1;
	if (!far || hold > 0)
	{
		state = (uint8_t)((hold > 0 ? hold - 1 : 0) << 1);
		return FarStep{ false, 1 };
	}
	state ^= 1;
	return (state & 1) != 0 ? FarStep{ true, 1 } : FarStep{ false, 2 };
}

void ComputeFarKeepCount(FarStep &step, int &movecount)
{
	if (!step.skip)
		return;
	if (movecount > 0)
		movecount--;
	else
		step = FarStep{ false, 1 };
}

void ComputeFarHold(uint8_t &state, int calls)
{
	state = (uint8_t)((calls < 0 ? 0 : calls > 127 ? 127 : calls) << 1);
}

void ComputeJamCount(uint8_t &count, bool failedEverywhere)
{
	count = !failedEverywhere ? 0 : count < 255 ? (uint8_t)(count + 1) : count;
}

bool ComputeJammed(uint8_t count, int after)
{
	return (int)count >= after;
}

int64_t ComputeAproxDistance(int64_t dx, int64_t dy)
{
	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	return dx + dy - ((dx < dy ? dx : dy) >> 1);
}

bool ComputeFarFromViewers(int64_t x, int64_t y, const int64_t *viewerX, const int64_t *viewerY, int count, int64_t threshold)
{
	if (threshold <= 0 || count <= 0)
		return false;
	for (int i = 0; i < count; i++)
	{
		if (ComputeAproxDistance(x - viewerX[i], y - viewerY[i]) < threshold)
			return false;
	}
	return true;
}

int ComputeStuckMoveCount(int movecount, int random)
{
	return movecount < 0 ? (random & 15) : movecount;
}

int ComputeMoveSteps(int64_t dx, int64_t dy, int64_t radius)
{
	const int64_t maxmove = radius - 65536;
	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	const int64_t longest = dx > dy ? dx : dy;
	return maxmove > 0 && longest > maxmove ? (int)(1 + longest / maxmove) : 1;
}

bool ComputeJamWait(uint8_t &countdown)
{
	if (countdown == 0)
		return false;
	countdown--;
	return true;
}

bool ComputeStillBlocks(const Box &walker, int64_t maxStep, int64_t destX, int64_t destY, const Box &blocker, bool infinitelyTall)
{
	const int64_t blockdist = blocker.radius + walker.radius;
	const int64_t dx = blocker.x - destX, dy = blocker.y - destY;
	if (dx >= blockdist || -dx >= blockdist || dy >= blockdist || -dy >= blockdist)
		return false;
	if (!infinitelyTall)
	{
		const int64_t top = blocker.z + blocker.height;
		if (walker.z >= top || walker.z + walker.height <= blocker.z || top - walker.z <= maxStep)
			return false;
	}
	return ComputeAproxDistance(dx, dy) <= ComputeAproxDistance(blocker.x - walker.x, blocker.y - walker.y);
}

int ComputeMemoSlot(const BlockMemo &memo, int dir)
{
	return (int)((memo.dirSlots >> (dir * 4)) & 15) - 1;
}

bool ComputeMemoRefresh(BlockMemo &memo, int now, int maxAge)
{
	if (now - memo.since > maxAge)
	{
		memo.dirSlots = 0;
		memo.lines = 0;
	}
	return memo.dirSlots != 0 || memo.lines != 0;
}

uint8_t ComputeMemoLines(const BlockMemo &memo, int64_t x, int64_t y, int64_t z, uint32_t epoch)
{
	return memo.x == x && memo.y == y && memo.z == z && memo.epoch == epoch ? memo.lines : 0;
}

void ComputeMemoForgetSlot(BlockMemo &memo, int slot)
{
	for (int dir = 0; dir < kMemoDirs; dir++)
	{
		if (ComputeMemoSlot(memo, dir) == slot)
			memo.dirSlots &= ~(15u << (dir * 4));
	}
}

// [Doruk-Aksoy] An empty memo restarts its age at now.
static void Start(BlockMemo &memo, int now)
{
	if (memo.dirSlots == 0 && memo.lines == 0)
		memo.since = now;
}

int ComputeMemoRecordActor(BlockMemo &memo, int dir, int same, int now)
{
	Start(memo, now);
	int slot = same;
	for (int s = 0; s < kMemoSlots && slot < 0; s++)
	{
		bool used = false;
		for (int d = 0; d < kMemoDirs; d++)
			used = used || ComputeMemoSlot(memo, d) == s;
		if (!used)
			slot = s;
	}
	if (slot < 0)
		return -1;
	memo.dirSlots = (memo.dirSlots & ~(15u << (dir * 4))) | ((uint32_t)(slot + 1) << (dir * 4));
	return slot;
}

void ComputeMemoRecordLines(BlockMemo &memo, uint8_t lines, int64_t x, int64_t y, int64_t z, uint32_t epoch, int now)
{
	Start(memo, now);
	if (memo.x != x || memo.y != y || memo.z != z || memo.epoch != epoch)
		memo.lines = 0;
	memo.x = x;
	memo.y = y;
	memo.z = z;
	memo.epoch = epoch;
	memo.lines |= lines;
}

} } // namespace zx::chasetrim
