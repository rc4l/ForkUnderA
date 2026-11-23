// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: interest_compute.cpp
//
// Description: The server's interest scoring. See interest_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>

#include "features/net-snapshot/computation/interest_compute.h"

using namespace zx::netsnapshot;

// Both missile floors lie inside the ranked bands, so a floor never passes the
// last of them.
typedef char snap_assert_missile_floors[((SNAPCL_TIER_DIST + SNAPCL_HOMING_FLOOR
	<= SNAPCL_TIER_DISTLAST) && (SNAPCL_TIER_DIST + SNAPCL_BALLISTIC_FLOOR
	<= SNAPCL_TIER_DISTLAST)) ? 1 : -1];

// Distance bands in map units, squared.
static const long long g_tierDistSq[SNAPCL_DIST_RINGS] =
{
	512LL * 512LL,
	1024LL * 1024LL,
	2048LL * 2048LL,
	4096LL * 4096LL,
	8192LL * 8192LL,
};

unsigned int zx::netsnapshot::ComputeRefreshBand(BYTE mask)
{
	return ((mask & (SNAPGRP_POS | SNAPGRP_POSZ)) != 0) ? SNAPCL_TIER_REFRESH : SNAPCL_TIER_ANIMREFRESH;
}

// `in.mask` is the FULL record mask, SNAPGRP_ENTER included: the flag is part
// of the decision, so the caller must not strip it. distBucket and bInFront
// are census observations and do not affect the tier: distBucket indexes
// the rings (0 = nearest), bInFront is the sign of the view dot.
// bCosmetic and bCulled mean "we decided not to send this", not "we tried
// and failed"; the caller stamps the aging clock for both.
unsigned int zx::netsnapshot::ComputeTier(const SnapTierIn& in, SnapTierOut& out)
{
	out.bForced = false;
	out.bCosmetic = false;
	out.bCulled = false;
	out.distBucket = SNAPCL_DIST_BUCKETS - 1;	// default: beyond the last ring
	out.bInFront = true;
	out.terminal = SNAP_TERMINAL_NONE;

	// Computed before any early return, so the census sees real values for
	// enters and players; the tier below reuses it.
	unsigned int distBucketLocal = SNAPCL_DIST_BUCKETS - 1;
	if (in.bView)
	{
		const long long dxv = (long long)PosToUnits(in.qx) - (long long)(in.viewX >> FRACBITS);
		const long long dyv = (long long)PosToUnits(in.qy) - (long long)(in.viewY >> FRACBITS);

		const long long dsq = dxv * dxv + dyv * dyv;
		for (unsigned int i = 0; i < SNAPCL_DIST_RINGS; ++i)
		{
			if (dsq < g_tierDistSq[i])
			{
				distBucketLocal = i;
				break;
			}
		}
		out.distBucket = distBucketLocal;
		out.bInFront = ((double)dxv * in.viewCos + (double)dyv * in.viewSin) >= 0.0;
	}

	// The starvation guard runs after classification, so it rescues only
	// records that competed and lost.

	// Players are unconditional: there are at most a handful of them, they are
	// what the eye tracks, and gameplay turns on their exact position.
	if (in.pri & SNAPPRI_PLAYER)
		return SNAPCL_TIER_TOP;

	// A death rides with the enters, as a transition the client cannot reach,
	// and only a death, which has no follow-up record to starve.
	if (in.bTransition)
		return SNAPCL_TIER_ENTER;

	// An entity the client lacks outranks a refinement, graded by distance
	// only as fua_snap_entergrade says.
	if (in.mask & SNAPGRP_ENTER)
	{
		// With no viewpoint (demo, intermission) distance is unknown: all
		// enters to ENTER.
		const bool grade = (in.enterGrade == 1) || ((in.enterGrade == 2) && in.bFillMode);
		if ((grade == false) || (in.bView == false))
			return SNAPCL_TIER_ENTER;
		// Graded: under 1024 units to ENTER, under 4096 to the near band,
		// the rest one band back, which still drains.
		if (distBucketLocal <= 1)
			return SNAPCL_TIER_ENTER;
		if (distBucketLocal <= 3)
			return SNAPCL_TIER_DIST;			// mid enters: near band, right behind TOP
		return SNAPCL_TIER_DIST + 1;			// far enters: one band back, still drains
	}

	if (in.bView == false)
	{
		// No viewpoint: neutral band, aging does the work.
		if (in.age >= SNAPCL_AGE_FORCE)
		{
			out.bForced = true;
			return SNAPCL_TIER_TOP;
		}
		return SNAPCL_TIER_DIST + 1;
	}

	// The distance bucket from the top, offset into the DIST tiers; beyond the
	// last ring maps to the last distance band.
	const long long dx = (long long)PosToUnits(in.qx) - (long long)(in.viewX >> FRACBITS);
	const long long dy = (long long)PosToUnits(in.qy) - (long long)(in.viewY >> FRACBITS);

	unsigned int tier = (distBucketLocal < SNAPCL_DIST_RINGS)
		? (SNAPCL_TIER_DIST + distBucketLocal) : (SNAPCL_TIER_DIST + SNAPCL_DIST_RINGS);

	// Behind the viewpoint is demoted, before the missile block, so its
	// floor clamps and missiles reach the starvation guard.
	const double dot = (double)dx * in.viewCos + (double)dy * in.viewSin;
	const bool behind = (dot < 0.0);
	if (behind)
		tier += 2;

	// Was a missile, not is one, since P_ExplodeMissile clears MF_MISSILE.
	if (in.pri & (SNAPPRI_MISSILE | SNAPPRI_TERMINAL))
	{
		const long long dsqM = dx * dx + dy * dy;
		// Its own radius, not one derived from fua_snap_cull_dist, so changing
		// the cull does not move missile priority.
		const long long threatSq = (long long)in.missileDist * (long long)in.missileDist;
		// Distance only, with no facing test, since a projectile crossing
		// back into view would arrive starved.
		const bool threatening = (dsqM <= threatSq);

		// Inside the radius every projectile goes to the missile band,
		// ballistic or not.
		if (threatening)
			tier = SNAPCL_TIER_MISSILE;
		else
			tier = (tier > SNAPCL_TIER_DIST) ? (tier - 1) : SNAPCL_TIER_DIST;

		// A band floor for every projectile, clamped so the guard still sees
		// it, tighter for steering ones, which cannot be extrapolated.
		const unsigned int floorTier = SNAPCL_TIER_DIST
			+ ((in.pri & SNAPPRI_HOMING) ? SNAPCL_HOMING_FLOOR : SNAPCL_BALLISTIC_FLOOR);
		if (tier > floorTier)
			tier = floorTier;
	}

	// Out of view for the cull below: dot < cullHalfCos * |d|.
	bool outOfView = false;
	{
		const double dlen = sqrt((double)dx * dx + (double)dy * dy);
		outOfView = (dot < in.cullHalfCos * dlen);

		// The cone does not apply close in, where a monster behind is one
		// turn from being in front.
		if (outOfView)
		{
			const long long nearSq = (long long)SNAPCL_CONE_NEAR * (long long)SNAPCL_CONE_NEAR;

			if ((dx * dx + dy * dy) <= nearSq)
				outOfView = false;
		}
	}

	// The interest cull, out of view or past fua_snap_cull_dist, missiles and
	// terminal sequences exempt (clienttrack.md, The bands).
	if ((in.pri & (SNAPPRI_MISSILE | SNAPPRI_TERMINAL)) == 0)
	{
		const long long cullSq = (long long)in.cullDist * (long long)in.cullDist;
		const long long distSq = dx * dx + dy * dy;

		if (outOfView)
		{
			// Cannot be seen: the bounded cosmetic clock is adequate.
			if (in.refreshAge >= SNAPCL_AGE_COSMETIC)
				return ComputeRefreshBand(in.mask);
			out.bCosmetic = true;
			return SNAPCL_TIER_COSMETIC;
		}
		if (distSq > cullSq)
		{
			// Far but in view: the last ranked band, with a bounded floor and
			// no starvation guard, since a cull is a choice.
			if (in.refreshAge >= SNAPCL_AGE_COSMETIC)
				return ComputeRefreshBand(in.mask);
			out.bCulled = true;
			tier = SNAPCL_TIER_DISTLAST;
		}
	}

	// A distant record of nothing but an animation frame is cosmetic, withheld
	// and exempt from the guard, but a terminal sequence goes to ENTER, capped.
	if (in.pri & SNAPPRI_TERMINAL)
	{
		out.terminal = SNAP_TERMINAL_SEEN;

		if (in.mask & SNAPGRP_APPEAR)
		{
			if (in.terminalThisFrame < (unsigned int)SNAPCL_TERMINAL_MAX)
			{
				out.terminal = SNAP_TERMINAL_PROMOTED;
				return SNAPCL_TIER_ENTER;
			}

			out.terminal = SNAP_TERMINAL_CAPPED;
		}
	}
	if (((in.mask & SNAPGRP_GROUPMASK) == SNAPGRP_APPEAR)
		&& (tier >= SNAPCL_TIER_DIST + 2)
		&& ((in.pri & SNAPPRI_TERMINAL) == 0))
	{
		// Withheld, not forever, or a far animating sprite stands still.
		if (in.refreshAge >= SNAPCL_AGE_COSMETIC)
			return ComputeRefreshBand(in.mask);

		out.bCosmetic = true;
		return SNAPCL_TIER_COSMETIC;
	}

	// The starvation guard: anything ranked and unsent long enough climbs.
	if (in.age >= (SNAPCL_AGE_FORCE * 2))
	{
		out.bForced = true;
		return SNAPCL_TIER_OVERDUE;
	}

	if (in.age >= SNAPCL_AGE_FORCE)
	{
		out.bForced = true;
		return SNAPCL_TIER_TOP;
	}
	if (in.age >= SNAPCL_AGE_PROMOTE)
		tier = (tier > SNAPCL_TIER_DIST + 2) ? (tier - 2) : SNAPCL_TIER_DIST;

	// Cosmetic band is reserved; ordinary records never fall into it.
	return (tier > SNAPCL_TIER_DISTLAST) ? SNAPCL_TIER_DISTLAST : tier;
}

void zx::netsnapshot::ComputeEventDelivery(bool bTerminal, int eventCopies, unsigned int& copies,
	unsigned int& spacing, unsigned int& deadline)
{
	int c = bTerminal ? SNAPCL_EVENT_TERMINAL_COPIES : eventCopies;
	int s = bTerminal ? SNAPCL_EVENT_TERMINAL_SPACING : 1;
	c = (c < 1) ? 1 : ((c > 4) ? 4 : c);
	s = (s < 1) ? 1 : ((s > 5) ? 5 : s);
	copies = (unsigned int)c;
	spacing = (unsigned int)s;
	// At the clamps at most 4 + 3 x 5 = 19 frames, inside SNAP_TERMINAL_TICS.
	deadline = SNAPCL_EVENT_DEADLINE + (copies - 1) * spacing;
}

unsigned int zx::netsnapshot::ComputeOwnEnterBucket(int wait)
{
	if (wait <= 0)	return 0;	// same tic it spawned on
	if (wait == 1)	return 1;
	if (wait == 2)	return 2;
	if (wait <= 4)	return 3;
	if (wait <= 8)	return 4;
	return 5;
}
