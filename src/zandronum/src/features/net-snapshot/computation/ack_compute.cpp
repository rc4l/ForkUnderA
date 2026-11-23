//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: ack_compute.cpp
//
// Description: The server's reading of a client's ack. See
// ack_compute.h.
//
//-----------------------------------------------------------------------------

#include "features/net-snapshot/computation/ack_compute.h"

using namespace zx::netsnapshot;

SnapAckRead zx::netsnapshot::ComputeAckRead(bool bHaveAck, unsigned int lastAckedSeq, unsigned int seq,
	unsigned int nextSeq)
{
	SnapAckRead r = { false, false, false, false };

	// An ack for a sequence not yet sent, from another stream or a bad client,
	// would make every true ack after it stale.
	if (seq >= nextSeq)
	{
		r.bAhead = true;
		return r;
	}

	// An older ack is stale, but the same sequence is processed, since a late
	// datagram's bit rides an ack under it.
	if (bHaveAck && (seq < lastAckedSeq))
	{
		r.bStale = true;
		return r;
	}

	r.bWide = (bHaveAck && ((seq - lastAckedSeq) > SNAP_ACK_HISTORY));
	r.bCanReadBits = (bHaveAck && ((seq - lastAckedSeq) <= SNAP_ACK_HISTORY));
	return r;
}

// Received means processed, since OnSnapshot applies or rejects every
// datagram it acks, out of order included.
bool zx::netsnapshot::ComputeProvenDelivered(unsigned int ackSeq, unsigned int history, bool bCanReadBits,
	unsigned int pfSeq)
{
	if (pfSeq == ackSeq)
		return true;			// the acked datagram itself, by definition
	if ((bCanReadBits == false) || (pfSeq > ackSeq))
		return false;

	const unsigned int back = ackSeq - pfSeq;
	if (back > SNAP_ACK_HISTORY)
		return false;			// outside the mask the client sent

	return ((history & (1u << (back - 1))) != 0);
}

int zx::netsnapshot::ComputeAckVerdict(unsigned int ackSeq, unsigned int history, bool bCanReadBits,
	unsigned int pfSeq)
{
	if (pfSeq > ackSeq)
		return SNAP_ACK_AHEAD;
	if (ComputeProvenDelivered(ackSeq, history, bCanReadBits, pfSeq))
		return SNAP_ACK_DELIVERED;

	if (bCanReadBits && ((ackSeq - pfSeq) < SNAP_REORDER_WINDOW))
		return SNAP_ACK_WAIT;

	return ((bCanReadBits == false) || ((ackSeq - pfSeq) > SNAP_ACK_HISTORY))
		? SNAP_ACK_LOST_UNPROVEN : SNAP_ACK_LOST;
}

BYTE zx::netsnapshot::ComputeAckLag(bool bHaveAck, unsigned int lastAckedSeq, unsigned int seq)
{
	if (bHaveAck == false)
		return SNAP_ACK_LAG_NONE;
	const unsigned int lag = seq - lastAckedSeq;
	if (lag == 0)
		return 1;			// an ack names an earlier datagram; 0 means none
	return (lag >= SNAP_ACK_LAG_FAR) ? (BYTE)SNAP_ACK_LAG_FAR : (BYTE)lag;
}

bool zx::netsnapshot::ComputeAckSettled(unsigned int seq, unsigned int lag, unsigned int missingBack)
{
	if (missingBack == 0)
	{
		// Every datagram the stream has sent before `seq` is in the history.
		if (seq <= SNAP_ACK_HISTORY + 1)
			return true;
		missingBack = SNAP_ACK_HISTORY + 1;
	}

	// The newest missing datagram is resolved by the echoed ack only if it is
	// SNAP_REORDER_WINDOW behind it, which a far lag never is.
	return (lag != SNAP_ACK_LAG_NONE) && ((lag + SNAP_REORDER_WINDOW) <= missingBack);
}

bool zx::netsnapshot::ComputeAckTooLate(unsigned int seq, unsigned int lastAckSeq, unsigned int history)
{
	if (seq >= lastAckSeq)
		return false;
	const unsigned int back = lastAckSeq - seq;
	// Past the history no ack names it, so the server has declared it lost.
	if (back > SNAP_ACK_HISTORY)
		return true;
	if (back < SNAP_REORDER_WINDOW)
		return false;
	// A set bit is a duplicate of a datagram that arrived in time.
	return (history & (1u << (back - 1))) == 0;
}

void zx::netsnapshot::ComputeAckFold(unsigned int seq, unsigned int& lastAckSeq, unsigned int& history)
{
	if (seq > lastAckSeq)
	{
		const unsigned int shift = seq - lastAckSeq;
		// A gap wider than the mask leaves everything in it unknown.
		history = (shift >= SNAP_ACK_HISTORY) ? 0 : ((history << shift) | (1u << (shift - 1)));
		lastAckSeq = seq;
		return;
	}
	const unsigned int back = lastAckSeq - seq;
	if ((back > 0) && (back < SNAP_ACK_HISTORY))
		history |= 1u << (back - 1);
}

unsigned int zx::netsnapshot::ComputeLostMaskAdvance(unsigned int mask, bool bHaveAck, unsigned int lastAckedSeq,
	unsigned int seq)
{
	if ((bHaveAck == false) || (seq < lastAckedSeq))
		return 0;
	const unsigned int shift = seq - lastAckedSeq;
	return (shift >= SNAP_ACK_HISTORY) ? 0 : (mask << shift);
}

unsigned int zx::netsnapshot::ComputeLostMaskMark(unsigned int mask, unsigned int seq, unsigned int lostSeq)
{
	if (lostSeq >= seq)
		return mask;
	const unsigned int back = seq - lostSeq;
	return (back > SNAP_ACK_HISTORY) ? mask : (mask | (1u << (back - 1)));
}

unsigned int zx::netsnapshot::ComputeSetAfterLost(unsigned int mask, unsigned int history)
{
	unsigned int both = mask & history;
	unsigned int n = 0;
	for (; both != 0; both &= both - 1)
		++n;
	return n;
}
