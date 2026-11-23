//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: join_compute.h
//
// Description: Which map a stream belongs to, over plain values: how far a
// datagram's map stamp is from the joined map's, and the whole-unit extent the
// server's map frame is built around. Pure: entities.cpp and
// capture.cpp read the stamp, the level and the actors.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_JOIN_COMPUTE_H__
#define __SNAP_JOIN_COMPUTE_H__

#include "basictypes.h"		// BYTE, fixed_t

namespace zx { namespace netsnapshot {

// How many maps `stamp` is past the joined map's stamp, -128 to 127, the stamps
// counting map loads modulo 256: 0 is the joined map, negative one already torn
// down, positive one not yet loaded.
int ComputeMapStampAhead(BYTE stamp, BYTE joinedStamp);

// Widens [lo, hi] on axis k to take the whole-unit value of v, floored and
// ceiled so a fraction cannot fall outside.
void ComputeExtentTake(long long lo[3], long long hi[3], unsigned int k, fixed_t v);

}} // namespace zx::netsnapshot

#endif // __SNAP_JOIN_COMPUTE_H__
