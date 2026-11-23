//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: replay_compute.h
//
// Description: The replay's statistics over plain values: the distance between
// two positions in whole units, a histogram of errors in 0.1 u bins and its
// p99, and the sums of the smoothing census that the replay's plan C refills
// add, take back and split. Pure: csnap_replay.cpp walks the recording and the
// ghosts and reports what these count.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_REPLAY_COMPUTE_H__
#define __SNAP_REPLAY_COMPUTE_H__

#include "basictypes.h"				// fixed_t
#include "features/net-snapshot/computation/smooth_compute.h"	// SnapSmoothStats

namespace zx { namespace netsnapshot {

// The distance between two positions, in whole map units, truncated.
unsigned int ComputeDistUnits(fixed_t x0, fixed_t y0, fixed_t z0,
	fixed_t x1, fixed_t y1, fixed_t z1);

}} // namespace zx::netsnapshot

// Ballistic draw error bins, 0.1 u each; the last also holds everything past.
#define SNAP_REPLAY_BINS		4096

namespace zx { namespace netsnapshot {

// One error into a histogram of SNAP_REPLAY_BINS 0.1 u bins, and its p99 as the
// upper edge of the bin it falls in.
void ComputeBinAdd(unsigned int* hist, double err);
double ComputeBinP99(const unsigned int* hist, unsigned int n);

// One smoothing census added into another, or taken back out of it; it must
// list every SnapSmoothStats field, or a new one is counted on each refill.
void ComputeCensusAdd(SnapSmoothStats& d, const SnapSmoothStats& s, bool bTake);

// The fold half of a smoothing census, which every refill adds, and the drawn
// half, which a tic's latest refill replaces.
void ComputeCensusSplit(const SnapSmoothStats& s, SnapSmoothStats& fold,
	SnapSmoothStats& drawn);

}} // namespace zx::netsnapshot

#endif // __SNAP_REPLAY_COMPUTE_H__
