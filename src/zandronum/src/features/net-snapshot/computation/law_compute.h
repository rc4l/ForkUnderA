// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: law_compute.h
//
// Description: The server's motion laws over plain values: the gravity law's
// acceleration, and the per-tic step of an entity's straight and gravity
// runs, which set the ballistic bits (design doc 5.2). Pure:
// capture.cpp reads the actor and the level, keeps the runs per id, and
// counts by what these return.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_LAW_COMPUTE_H__
#define __SNAP_LAW_COMPUTE_H__

#include "basictypes.h"		// fixed_t, WORD, BYTE, FRACUNIT

//*****************************************************************************
//	GRAVITY

namespace zx { namespace netsnapshot {

// The gravity law's acceleration in fixed units per tic per tic, GetGravity's
// arithmetic over the level's, sector's and class's gravity, truncated alike.
double ComputeGravityAccel(float levelGravity, float sectorGravity, fixed_t classGravity);

}} // namespace zx::netsnapshot

//*****************************************************************************
//	STRAIGHT AND GRAVITY RUNS

// Heading tolerance, about 1 degree, which a real turn passes and quantization
// and floor friction do not.
#define SNAP_STRAIGHT_TOL	180

// How much velz may change a tic and still hold a heading: at most 6 units over
// SNAP_BALLISTIC_MAX_TICS, where a quarter unit would pass weak gravity.
#define SNAP_STRAIGHT_ZTOL	(FRACUNIT / 16)
// The gravity test's second difference, exactly 0 on the server.
#define SNAP_GRAV_ZTOL		(FRACUNIT / 4)

// Tics on one heading before an entity is trusted, below the four or more of a
// missile's run and above the one or two of anything turning.
#define SNAP_STRAIGHT_MIN	3

// One id's runs as the previous tic left them.
struct SnapRunTrack
{
	WORD		lastDir;		// BAM >> 16, or 0xFFFF for 'was not moving'
	fixed_t		lastVelZ;		// velz at the previous tic
	fixed_t		lastDVelZ;		// velz's change at the previous tic
	BYTE		straightRun;	// consecutive tics on the same heading
	BYTE		gravRun;		// consecutive tics of constant acceleration
};

// What one tic's step found.
struct SnapRunStep
{
	bool		bMoving;		// moving in plan this tic
	bool		bCompared;		// moving, with a previous heading to compare against
	bool		bHeld;			// compared, and the heading and velz held
	int			endedBucket;	// the run a turn ended, 1, 2-3, 4-7, 8-15, 16+ tics; -1 if none
};

namespace zx { namespace netsnapshot {

// Advances one id's runs by this tic's velocity (capture.md): still in plan
// they clear, and a turn records the run it ended and starts one of a tic.
SnapRunStep ComputeRunStep(SnapRunTrack& track, fixed_t velx, fixed_t vely, fixed_t velz);

}} // namespace zx::netsnapshot

#endif // __SNAP_LAW_COMPUTE_H__
