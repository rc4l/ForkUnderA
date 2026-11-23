// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: wall_compute.cpp
//
// Description: The mirrors' wall clamp. See wall_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>

#include "doomdef.h"			// TICRATE
#include "features/net-snapshot/format.h"		// MissileFlagged
#include "features/net-snapshot/computation/wall_compute.h"

using namespace zx::netsnapshot;

void zx::netsnapshot::ComputeSweepSlab(double o, double d, double lo, double hi, double & s0, double & s1)
{
	if (fabs(d) < 1e-9)
	{
		if ((o <= lo) || (o >= hi))
			s1 = s0 - 1.0;			// never inside
		return;
	}
	double a = (lo - o) / d, b = (hi - o) / d;
	if (a > b) { const double tmp = a; a = b; b = tmp; }
	if (a > s0) s0 = a;
	if (b < s1) s1 = b;
}

bool zx::netsnapshot::ComputeSweepLine(double ox, double oy, double dx, double dy, double r,
	double x1, double y1, double x2, double y2, double & s)
{
	const double lx = x2 - x1, ly = y2 - y1;
	const double len = sqrt(lx * lx + ly * ly);
	if (len <= 0.0)
		return false;
	double s0 = -1e30, s1 = 1e30;
	ComputeSweepSlab(ox, dx, ((x1 < x2) ? x1 : x2) - r, ((x1 > x2) ? x1 : x2) + r, s0, s1);
	ComputeSweepSlab(oy, dy, ((y1 < y2) ? y1 : y2) - r, ((y1 > y2) ? y1 : y2) + r, s0, s1);
	const double nx = -ly / len, ny = lx / len;
	const double e = r * (fabs(nx) + fabs(ny));
	ComputeSweepSlab((ox - x1) * nx + (oy - y1) * ny, dx * nx + dy * ny, -e, e, s0, s1);
	s = s0;
	return s0 < s1;
}

bool zx::netsnapshot::ComputeWallPath(fixed_t vx, fixed_t vy, unsigned int exCapMS, fixed_t radius,
	fixed_t ox, fixed_t oy, SnapWallPath& p)
{
	const double F = 65536.0;
	const double fvx = (double)vx / F, fvy = (double)vy / F;
	const double speed = sqrt(fvx * fvx + fvy * fvy);		// units per tic
	if (speed <= 0.0)
		return false;
	p.speed = speed;
	p.r = (double)radius / F;
	p.reach = speed * (double)exCapMS * (double)TICRATE / 1000.0;
	p.traceLen = p.reach + p.r * 1.41421356;
	p.dx = fvx / speed;
	p.dy = fvy / speed;
	p.ox = (double)ox / F;
	p.oy = (double)oy / F;
	return true;
}

bool zx::netsnapshot::ComputeWallProjectile(bool bMissile, bool bBounceMBF, bool bMonster, WORD misc)
{
	return bMissile || bBounceMBF || ((bMonster == false) && MissileFlagged(misc));
}

bool zx::netsnapshot::ComputeWallContact(const SnapWallPath& p, double x1, double y1, double lx, double ly,
	double frac, double best, double oz, double vz, double accelZ, SnapWallContact& c)
{
	const double len2 = lx * lx + ly * ly;
	if (len2 <= 0.0)
		return false;
	// The centre's crossing if a zero radius leaves no interval.
	double s;
	if (ComputeSweepLine(p.ox, p.oy, p.dx, p.dy, p.r, x1, y1, x1 + lx, y1 + ly, s) == false)
		s = p.traceLen * frac;
	if (s < 0.0)
		s = 0.0;
	if ((s > p.reach) || ((best >= 0.0) && (s >= best)))
		return false;

	// The engine's opening point: the segment's nearest to the centre.
	const double cx = p.ox + p.dx * s, cy = p.oy + p.dy * s;
	double u = ((cx - x1) * lx + (cy - y1) * ly) / len2;
	u = (u < 0.0) ? 0.0 : ((u > 1.0) ? 1.0 : u);
	const double t = s / p.speed;
	c.s = s;
	c.px = x1 + lx * u;
	c.py = y1 + ly * u;
	c.cx = cx;
	c.cy = cy;
	c.z = oz + vz * t + 0.5 * accelZ * t * t;
	return true;
}

bool zx::netsnapshot::ComputeLineFlagsBlock(const SnapLineFlags& l, const SnapBodyFlags& b, bool bProjectile,
	bool& bRail)
{
	bRail = false;
	if ((bProjectile == false) || l.bBlockEverything || l.bBlockProjectile)
	{
		if (l.bRailing)
			bRail = true;
		else if (l.bBlocking || l.bBlockEverything
			|| ((b.bNoBlockMonst == false) && l.bBlockMonsters)
			|| (bProjectile && l.bBlockProjectile)
			|| (b.bFloat && l.bBlockFloaters))
			return true;
	}
	return false;
}

bool zx::netsnapshot::ComputeOpeningBlocks(const SnapBodyFlags& b, double top, double bottom, double z)
{
	const double h = b.height;
	if ((top - bottom) < h)
		return true;
	if ((b.bCeilingHugger == false) && ((z + h) > top))
		return true;
	if (b.bFloorHugger == false)
	{
		if (b.bMissile && (b.bStepMissile == false))
		{
			if (bottom > z)
				return true;		// missiles do not climb steps
		}
		else if ((bottom - z) > b.maxStep)
			return true;
	}
	return false;
}
