//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: wall_compute.h
//
// Description: The mirrors' wall clamp: how far an extrapolated ghost may go
// along its path before its box touches a line, and whether that line stops
// a body of its kind, from PIT_CheckLine's flags and P_TryMove's opening
// tests. Pure: ghostactor.cpp walks the lines, reads the flags and the
// opening, and keeps the census.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_WALL_COMPUTE_H__
#define __SNAP_WALL_COMPUTE_H__

#include "basictypes.h"		// fixed_t, WORD

namespace zx { namespace netsnapshot {

// Narrow [s0, s1] to where o + d*s lies strictly inside (lo, hi).
void ComputeSweepSlab(double o, double d, double lo, double hi, double & s0, double & s1);

// PIT_CheckLine's contact test swept from (ox, oy) along (dx, dy) for a box of
// half-width r and the segment (x1, y1) to (x2, y2): the box overlaps the
// segment's bounding box and straddles its line, false if never both.
bool ComputeSweepLine(double ox, double oy, double dx, double dy, double r,
	double x1, double y1, double x2, double y2, double & s);

}} // namespace zx::netsnapshot

// An extrapolated ghost's path in map units, from (ox, oy) along (dx, dy) at
// `speed` a tic, up to `reach`, traced `traceLen`.
struct SnapWallPath
{
	double			ox, oy;
	double			dx, dy;
	double			speed;
	double			r;				// the box's half-width
	double			reach;
	double			traceLen;
};

// Where a path first touches a line: `s` along it, the opening point, the
// centre there, and the body's height.
struct SnapWallContact
{
	double			s;
	double			px, py;			// the opening point
	double			cx, cy;			// the centre
	double			z;
};

// What a line's flags say to a moving body.
struct SnapLineFlags
{
	bool			bBlockEverything;
	bool			bBlockProjectile;
	bool			bRailing;
	bool			bBlocking;
	bool			bBlockMonsters;
	bool			bBlockFloaters;
};

// The body's defaults that the line and opening tests read.
struct SnapBodyFlags
{
	bool			bNoBlockMonst;
	bool			bFloat;
	bool			bCeilingHugger;
	bool			bFloorHugger;
	bool			bMissile;
	bool			bStepMissile;
	double			height;			// fixed units
	double			maxStep;		// fixed units
};

namespace zx { namespace netsnapshot {

// The path of a pose's wire velocity (fixed units a tic) from its origin over
// its cap, for a box of `radius`; false when it does not move across.
bool ComputeWallPath(fixed_t vx, fixed_t vy, unsigned int exCapMS, fixed_t radius,
	fixed_t ox, fixed_t oy, SnapWallPath& p);

// Whether the lines treat a body as a projectile: a projectile by default, an
// MBF bouncer, or a non-monster whose wire bits flag it a missile.
bool ComputeWallProjectile(bool bMissile, bool bBounceMBF, bool bMonster, WORD misc);

// The path's contact with the segment from (x1, y1) along (lx, ly), `frac` of
// traceLen standing in for a zero radius, false for a zero-length line or one
// touched past the reach or no nearer than `best` (negative: none yet).
bool ComputeWallContact(const SnapWallPath& p, double x1, double y1, double lx, double ly,
	double frac, double best, double oz, double vz, double accelZ, SnapWallContact& c);

// Whether a two-sided line's flags stop the body outright; sets bRail for a
// railing, which raises the opening's bottom instead.
bool ComputeLineFlagsBlock(const SnapLineFlags& l, const SnapBodyFlags& b, bool bProjectile,
	bool& bRail);

// Whether an opening from `bottom` to `top` stops a body at height z: too
// short for it, its top above the opening, or a step too high to climb (a
// missile climbs none).
bool ComputeOpeningBlocks(const SnapBodyFlags& b, double top, double bottom, double z);

}} // namespace zx::netsnapshot

#endif // __SNAP_WALL_COMPUTE_H__
