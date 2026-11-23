// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: law_compute.cpp
//
// Description: The server's motion laws. See law_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>

#include "features/net-snapshot/computation/law_compute.h"

using namespace zx::netsnapshot;

double zx::netsnapshot::ComputeGravityAccel(float levelGravity, float sectorGravity, fixed_t classGravity)
{
	return (double)fixed_t(levelGravity * sectorGravity * ((float)classGravity / float(65536)) * 81.92);
}

SnapRunStep zx::netsnapshot::ComputeRunStep(SnapRunTrack& track, fixed_t velx, fixed_t vely, fixed_t velz)
{
	SnapRunStep step;
	step.bMoving = false;
	step.bCompared = false;
	step.bHeld = false;
	step.endedBucket = -1;

	// Walkers move via P_TryMove and report zero velocity, so they drop out
	// here rather than polluting the histogram with a heading of nothing.
	const double vx = (double)velx, vy = (double)vely;
	if ((vx == 0.0) && (vy == 0.0))
	{
		track.lastDir = 0xFFFF;
		track.lastVelZ = velz;
		track.lastDVelZ = 0;
		track.gravRun = 0;
		track.straightRun = 0;
		return step;
	}
	step.bMoving = true;

	const double ang = atan2(vy, vx);	// -pi..pi
	const WORD dir = (WORD)(((ang + 3.14159265358979) / 6.28318530717959) * 65535.0);
	const WORD prev = track.lastDir;

	if (prev == 0xFFFF)
		track.straightRun = 1;		// first moving tic: no history, run of one
	else
	{
		int d = (int)dir - (int)prev;
		if (d > 32767) d -= 65536;
		else if (d < -32767) d += 65536;
		if (d < 0) d = -d;
		// velz is folded into bHeld, so a run straight in plan but wrong in
		// Z counts as the mispredict it is.
		const int dvz = (int)velz - (int)track.lastVelZ;
		int dz = dvz;
		if (dz < 0) dz = -dz;
		const bool bHeld = (d <= SNAP_STRAIGHT_TOL) && (dz <= SNAP_STRAIGHT_ZTOL);
		step.bCompared = true;
		step.bHeld = bHeld;

		// Constant acceleration: the plan heading holds while velz changes
		// by the same amount each tic.
		{
			int d2z = dvz - (int)track.lastDVelZ;
			if (d2z < 0) d2z = -d2z;
			if ((d <= SNAP_STRAIGHT_TOL) && (d2z <= SNAP_GRAV_ZTOL))
			{
				if (track.gravRun < 255)
					++track.gravRun;
			}
			else
				track.gravRun = 1;
		}
		// Stored after the test reads it: stored before, the second difference
		// would always be zero.
		track.lastDVelZ = (fixed_t)dvz;

		if (bHeld)
		{
			if (track.straightRun < 255)
				++track.straightRun;
		}
		else
		{
			// Record the run that just ENDED, then start a new one, so each run
			// is counted once at its true length.
			const int run = track.straightRun;
			step.endedBucket = (run <= 1) ? 0 : (run <= 3) ? 1 : (run <= 7) ? 2
				: (run <= 15) ? 3 : 4;
			track.straightRun = 1;
		}
	}
	track.lastDir = dir;
	track.lastVelZ = velz;
	return step;
}
