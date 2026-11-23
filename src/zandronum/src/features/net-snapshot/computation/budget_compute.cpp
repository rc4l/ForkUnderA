//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: budget_compute.cpp
//
// Description: The server's byte budgets. See budget_compute.h.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/budget_compute.h"

using namespace zx::netsnapshot;

unsigned int zx::netsnapshot::ComputePacketRoom(unsigned int maxPacket)
{
	return (maxPacket > SNAP_PACKET_OVERHEAD + 64)
		? (maxPacket - SNAP_PACKET_OVERHEAD) : 64;
}

unsigned int zx::netsnapshot::ComputeEffectiveBudget(int maxBytes)
{
	return (maxBytes > 64) ? (unsigned int)maxBytes : 64;
}

unsigned int zx::netsnapshot::ComputeChunkLimit(unsigned int maxPacket)
{
	return ComputePacketRoom(maxPacket);
}

unsigned int zx::netsnapshot::ComputeChunkCuts(const unsigned int* bounds, unsigned int nBounds,
	unsigned int chunkBytes, unsigned int* cuts)
{
	const unsigned int estBytes = bounds[nBounds - 1];
	unsigned int cut = chunkBytes;
	if ((cut == 0) || (cut >= estBytes))
		cut = estBytes;				// one chunk; no restart, so the estimate is exact
	else
		--cut;						// room for the opener's raw id

	// bounds[b] == start throughout, both cursors moving forward only.
	unsigned int nCuts = 0;
	unsigned int start = 0;
	unsigned int b = 0;
	while (start < estBytes)
	{
		cuts[nCuts++] = b;
		unsigned int j = b;
		while ((j + 1 < nBounds) && ((bounds[j + 1] - start) <= cut))
			++j;
		// A record wider than the chunk goes over on its own, or the chunk
		// would be empty and start would not advance.
		if (j == b)
			++j;
		start = bounds[j];
		b = j;
	}
	return nCuts;
}

int zx::netsnapshot::ComputeCosPickCmp(const void* a, const void* b)
{
	const SnapCosPick* pa = static_cast<const SnapCosPick*>(a);
	const SnapCosPick* pb = static_cast<const SnapCosPick*>(b);
	if (pa->dist != pb->dist)
		return (pa->dist < pb->dist) ? -1 : 1;
	return (int)pb->carried - (int)pa->carried;
}

unsigned int zx::netsnapshot::ComputeCosBankRefill(unsigned int bank, unsigned int perTic, unsigned int tics)
{
	const unsigned int sum = bank + perTic * tics;
	const unsigned int top = perTic * ((4u > tics) ? 4u : tics);
	return (sum < top) ? sum : top;
}

unsigned int zx::netsnapshot::ComputeCosFit(const SnapCosPick* picks, unsigned int n, unsigned int cap,
	unsigned int& sent)
{
	sent = 0;
	unsigned int keep;
	for (keep = 0; keep < n; ++keep)
	{
		const unsigned int len = picks[keep].len;
		if (sent + len > cap)
			break;
		sent += len;
	}
	return keep;
}

void zx::netsnapshot::ComputeCosOverflow(const SnapCosPick* picks, unsigned int n, unsigned int keep,
	unsigned int room, BYTE* verdicts)
{
	unsigned int carryBytes = 0;
	for (unsigned int k = keep; k < n; ++k)
	{
		if (picks[k].carried)
		{
			verdicts[k] = SNAP_COSO_CARRIED_AGAIN;
			continue;
		}
		if (carryBytes + picks[k].len > room)
		{
			verdicts[k] = SNAP_COSO_NO_ROOM;
			continue;
		}
		carryBytes += picks[k].len;
		verdicts[k] = SNAP_COSO_CARRY;
	}
}

bool zx::netsnapshot::ComputeCosAppends(int lastChunkLen, unsigned int sent, int maxPacket, int packetOverhead,
	unsigned int cmdHeader)
{
	const int left = maxPacket - packetOverhead - lastChunkLen;
	return (lastChunkLen >= 0) && ((int)(sent + cmdHeader + 5) <= left);
}
