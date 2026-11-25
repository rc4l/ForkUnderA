// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: mover_compute.h
//
// Description: The movers' decisions over plain values: the join's sector
// write order over the link graph, a mover class's report name, the flags
// that narrow a GONE, which polyobject actions turn, and the part of a
// polyobject's capture its dirty compare reads. Pure: movers.cpp,
// polyobj.cpp and capture.cpp read the level and the thinkers and
// act on what these return.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_MOVER_COMPUTE_H__
#define __SNAP_MOVER_COMPUTE_H__

#include "features/net-snapshot/format.h"		// BYTE, SnapMoverPoly, SNAPMV_, SNAPPO_

//*****************************************************************************
//	LINK ORDER

struct SnapLinkEdge
{
	int from;			// control sector
	int to;				// sector it drives
	int type;			// LINK_* movement type
};

namespace zx { namespace netsnapshot {

// The join's plane order into order[numSectors], each sector after every
// control driving it, in rounds by index, with drivenPlanes[numSectors] the
// planes a link moves (1 floor, 2 ceiling, by floorBits and ceilingBits);
// returns how many sectors on or behind a link cycle never free, appended last
// in index order.
int ComputeLinkWriteOrder(int numSectors, const SnapLinkEdge* edges, unsigned int nEdges,
	int floorBits, int ceilingBits, int* order, BYTE* drivenPlanes);

//*****************************************************************************
//	MOVER RECORDS

// A mover class's name for the report, "?" for a class without one; the
// tests require a name for every class below SNAPMV_COUNT.
const char* ComputeMoverClassName(BYTE cls);

// The flags that narrow a GONE off these planes.
BYTE ComputeMoverNoFlags(BYTE planes);

// Whether a polyobject action of this type turns rather than translates.
bool ComputePolyTurns(BYTE type);

// Clears the phases a polyobject's dirty compare leaves out: its position and
// angle, and each entry's speeds and countdowns (movers.md).
void ComputePolyCompareKey(SnapMoverPoly& s);

}} // namespace zx::netsnapshot

#endif // __SNAP_MOVER_COMPUTE_H__
