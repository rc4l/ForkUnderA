// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: ack_compute.h
//
// Description: The server's reading of a client's ack: whether it is read at
// all, whether its history covers everything since the last one, and the
// verdict on each pending datagram it reaches. Pure: clienttrack.cpp walks
// the client's pending datagrams, folds the delivered, repairs the lost and
// counts both by what these return. Also the ack lag each datagram echoes,
// and the client's test of whether the server had repaired every loss before
// a datagram when it built it, which decides whether its checksums count.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_ACK_COMPUTE_H__
#define __SNAP_ACK_COMPUTE_H__

#include "features/net-snapshot/format.h"		// SNAP_REORDER_WINDOW

// Datagrams before the acked one that an ack's history mask covers.
#define SNAP_ACK_HISTORY		32

// How an ack stands against the last one processed.
struct SnapAckRead
{
	bool			bStale;			// older than the last processed, so ignored
	bool			bWide;			// further past the last than its history reaches
	bool			bCanReadBits;	// its history covers every datagram since the last
	bool			bAhead;			// names a datagram not yet sent, so ignored
};

namespace zx { namespace netsnapshot {

// An ack's standing: the first one can be read only for the datagram it
// names, and the same sequence again is read, since a late datagram is acked
// under the sequence already seen.
SnapAckRead ComputeAckRead(bool bHaveAck, unsigned int lastAckedSeq, unsigned int seq,
	unsigned int nextSeq);

// Whether an ack PROVES a pending datagram arrived: it is the acked datagram,
// or the history is readable and has its bit set.
bool ComputeProvenDelivered(unsigned int ackSeq, unsigned int history, bool bCanReadBits,
	unsigned int pfSeq);

}} // namespace zx::netsnapshot

// What an ack says of one pending datagram, the walk stopping at AHEAD or WAIT.
enum
{
	SNAP_ACK_AHEAD,				// newer than the ack: not yet answered
	SNAP_ACK_DELIVERED,			// proven delivered
	SNAP_ACK_WAIT,				// bit clear but inside SNAP_REORDER_WINDOW: may only be late
	SNAP_ACK_LOST,				// bit clear and past the window
	SNAP_ACK_LOST_UNPROVEN,		// past the history, or the ack gap was wider: may have arrived
};

namespace zx { namespace netsnapshot {

int ComputeAckVerdict(unsigned int ackSeq, unsigned int history, bool bCanReadBits,
	unsigned int pfSeq);

}} // namespace zx::netsnapshot

// The ack lag a datagram echoes, its sequence less the newest processed ack
// (255 for 255 or more, 0 before any ack).
#define SNAP_ACK_LAG_NONE		0
#define SNAP_ACK_LAG_FAR		255

namespace zx { namespace netsnapshot {

BYTE ComputeAckLag(bool bHaveAck, unsigned int lastAckedSeq, unsigned int seq);

// Whether a record in datagram `seq` is settled: its echoed ack (seq - lag) is
// at least SNAP_REORDER_WINDOW past every datagram missing before it, the
// newest of which is `missingBack` behind `seq` (0: none in the history).
bool ComputeAckSettled(unsigned int seq, unsigned int lag, unsigned int missingBack);

// Whether a datagram arriving now is one the server has declared lost, the
// client having acked SNAP_REORDER_WINDOW past it while it was missing, or one
// past the history; `lastAckSeq` and `history` are the ack state before it.
bool ComputeAckTooLate(unsigned int seq, unsigned int lastAckSeq, unsigned int history);

// The client's ack state after datagram `seq` arrives: the newest sequence and
// the history behind it, a duplicate or one past the history setting no bit.
void ComputeAckFold(unsigned int seq, unsigned int& lastAckSeq, unsigned int& history);

// The server's record of the datagrams it declared lost, bit b-1 for the one b
// behind its newest processed ack: re-based to ack `seq`, a loss marked, and
// the bits a later history sets counted.
unsigned int ComputeLostMaskAdvance(unsigned int mask, bool bHaveAck, unsigned int lastAckedSeq,
	unsigned int seq);
unsigned int ComputeLostMaskMark(unsigned int mask, unsigned int seq, unsigned int lostSeq);
unsigned int ComputeSetAfterLost(unsigned int mask, unsigned int history);

}} // namespace zx::netsnapshot

#endif // __SNAP_ACK_COMPUTE_H__
