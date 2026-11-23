//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: replay_compute.cpp
//
// Description: The replay's statistics. See replay_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>
#include <string.h>

#include "features/net-snapshot/computation/replay_compute.h"

using namespace zx::netsnapshot;

unsigned int zx::netsnapshot::ComputeDistUnits(fixed_t x0, fixed_t y0, fixed_t z0,
	fixed_t x1, fixed_t y1, fixed_t z1)
{
	const double dx = (double)(x1 - x0) / 65536.0;
	const double dy = (double)(y1 - y0) / 65536.0;
	const double dz = (double)(z1 - z0) / 65536.0;
	const double d = sqrt(dx * dx + dy * dy + dz * dz);
	return (unsigned int)((d < 0.0) ? 0.0 : d);
}

void zx::netsnapshot::ComputeBinAdd(unsigned int* hist, double err)
{
	const double bin = err * 10.0;
	++hist[(bin < SNAP_REPLAY_BINS - 1) ? (unsigned int)bin : SNAP_REPLAY_BINS - 1];
}

double zx::netsnapshot::ComputeBinP99(const unsigned int* hist, unsigned int n)
{
	const unsigned int at = (unsigned int)(((QWORD)n * 99) / 100);
	unsigned int bin = 0, below = 0;
	while ((bin < SNAP_REPLAY_BINS - 1) && (below + hist[bin] <= at))
		below += hist[bin++];
	return (bin + 1) / 10.0;
}

// One count of a smoothing census into another, or taken back out of it.
static void census_Acc(unsigned int& d, unsigned int s, bool bTake)
{
	d = bTake ? (d - s) : (d + s);
}

void zx::netsnapshot::ComputeCensusAdd(SnapSmoothStats& d, const SnapSmoothStats& s, bool bTake)
{
	for (unsigned int p = 0; p < 2; ++p)
	{
		census_Acc(d.folds[p], s.folds[p], bTake);
		census_Acc(d.snaps[p], s.snaps[p], bTake);
		d.foldSum[p] += bTake ? -s.foldSum[p] : s.foldSum[p];
		for (unsigned int b = 0; b < 4; ++b)
			census_Acc(d.resid[p][b], s.resid[p][b], bTake);
	}
	census_Acc(d.offFrames, s.offFrames, bTake);
	census_Acc(d.past, s.past, bTake);
	census_Acc(d.zero, s.zero, bTake);
	census_Acc(d.reversed, s.reversed, bTake);
	census_Acc(d.movingFrames, s.movingFrames, bTake);
	for (unsigned int k = 0; k < SNAP_SMOOTH_KINDS; ++k)
	{
		for (unsigned int a = 0; a < 2; ++a)
		{
			SnapSmoothSplit& dc = d.ex[k][a];
			const SnapSmoothSplit& sc = s.ex[k][a];
			census_Acc(dc.folds, sc.folds, bTake);
			census_Acc(dc.snaps, sc.snaps, bTake);
			census_Acc(dc.snapsBack, sc.snapsBack, bTake);
			census_Acc(dc.reversed, sc.reversed, bTake);
			census_Acc(dc.moving, sc.moving, bTake);
			for (unsigned int b = 0; b < 3; ++b)
				census_Acc(dc.snapSize[b], sc.snapSize[b], bTake);
			census_Acc(dc.off15, sc.off15, bTake);
			census_Acc(dc.off45, sc.off45, bTake);
			dc.foldSum += bTake ? -sc.foldSum : sc.foldSum;
		}
	}
}

void zx::netsnapshot::ComputeCensusSplit(const SnapSmoothStats& s, SnapSmoothStats& fold,
	SnapSmoothStats& drawn)
{
	fold = s;
	fold.offFrames = fold.past = fold.reversed = fold.movingFrames = 0;
	memset(&drawn, 0, sizeof(drawn));
	drawn.offFrames = s.offFrames;
	drawn.past = s.past;
	drawn.reversed = s.reversed;
	drawn.movingFrames = s.movingFrames;
	for (unsigned int k = 0; k < SNAP_SMOOTH_KINDS; ++k)
	{
		for (unsigned int a = 0; a < 2; ++a)
		{
			fold.ex[k][a].reversed = fold.ex[k][a].moving = 0;
			fold.ex[k][a].off15 = fold.ex[k][a].off45 = 0;
			drawn.ex[k][a].reversed = s.ex[k][a].reversed;
			drawn.ex[k][a].moving = s.ex[k][a].moving;
			drawn.ex[k][a].off15 = s.ex[k][a].off15;
			drawn.ex[k][a].off45 = s.ex[k][a].off45;
		}
	}
}
