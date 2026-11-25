// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: budget_compute.h
//
// Description: The server's byte budgets: the snapshot command one datagram
// may carry, a frame's budget across datagrams, the cut of a frame into
// chunks, and the cosmetic event bank (its refill, the order and fit of one
// client's events, what overflows, and whether the command joins the last
// chunk's datagram). Pure: capture.cpp and clienttrack.cpp read the
// settings and the packet size, and count by what these return.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_BUDGET_COMPUTE_H__
#define __SNAP_BUDGET_COMPUTE_H__

#include "basictypes.h"		// BYTE

//*****************************************************************************
//	PACKET AND FRAME

// Bytes the payload leaves free in one unreliable packet: the command's header
// and the send path's slack, 27, rounded up (capture.md).
#define SNAP_PACKET_OVERHEAD	28

namespace zx { namespace netsnapshot {

// The largest snapshot command one datagram may carry at a packet size of
// maxPacket, never under 64.
unsigned int ComputePacketRoom(unsigned int maxPacket);

// Bytes the priority pass may spend on one frame, across all its datagrams:
// fua_snap_maxbytes, never under 64. Not clamped to the packet: that limit
// applies per chunk (ComputeChunkLimit).
unsigned int ComputeEffectiveBudget(int maxBytes);

// How much of the budget one datagram carries: the whole packet room, so a
// larger fua_snap_maxbytes means more datagrams, never one past its size.
unsigned int ComputeChunkLimit(unsigned int maxPacket);

// The record that opens each chunk of one client frame, cut at chunkBytes - 1
// (0: one chunk), returning the chunk count. `bounds` holds each of the n
// records' estimated start, ascending, then the payload's estimated size;
// `cuts` holds n entries.
unsigned int ComputeChunkCuts(const unsigned int* bounds, unsigned int nBounds,
	unsigned int chunkBytes, unsigned int* cuts);

}} // namespace zx::netsnapshot

//*****************************************************************************
//	COSMETIC EVENT BANK

// One client's candidate event in a pump (design doc 6.3).
struct SnapCosPick
{
	unsigned int	dist;
	unsigned int	index;		// into this pump's events, then the carried ones
	unsigned int	len;		// the event's bytes
	BYTE			carried;
};

namespace zx { namespace netsnapshot {

// SnapCosPick's qsort order: nearest first, a carried event winning a tie.
int ComputeCosPickCmp(const void* a, const void* b);

// The bank after a pump that simulated `tics`: refilled per tic, so a
// catch-up pump is not starved, and capped at four tics' worth, or the pump's
// own tics where more, so what is saved while quiet carries a burst.
unsigned int ComputeCosBankRefill(unsigned int bank, unsigned int perTic, unsigned int tics);

// How many of the sorted `picks` fit in `cap` bytes, the prefix up to the first
// that does not, with their bytes in `sent`.
unsigned int ComputeCosFit(const SnapCosPick* picks, unsigned int n, unsigned int cap,
	unsigned int& sent);

}} // namespace zx::netsnapshot

// What becomes of a pick past the fit.
enum
{
	SNAP_COSO_CARRY,			// carried to the next pump
	SNAP_COSO_CARRIED_AGAIN,	// already carried once: dropped
	SNAP_COSO_NO_ROOM,			// past what one datagram holds: dropped
};

namespace zx { namespace netsnapshot {

// The overflow of a capped pump: each pick from `keep` on is carried one pump
// while the carried bytes fit `room`, and dropped if already carried or if it
// does not fit, its SNAP_COSO_* verdict in `verdicts` (n entries).
void ComputeCosOverflow(const SnapCosPick* picks, unsigned int n, unsigned int keep,
	unsigned int room, BYTE* verdicts);

// Whether a cosmetic events command of `sent` body bytes, its `cmdHeader`
// bytes and SERVER_CheckClientBuffer's 5 fits in the datagram that carries
// the last chunk: maxPacket less the packet overhead and that chunk's
// lastChunkLen bytes. lastChunkLen is -1 when no chunk went out.
bool ComputeCosAppends(int lastChunkLen, unsigned int sent, int maxPacket, int packetOverhead,
	unsigned int cmdHeader);

}} // namespace zx::netsnapshot

#endif // __SNAP_BUDGET_COMPUTE_H__
