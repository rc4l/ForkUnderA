// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: interest_compute.h
//
// Description: The server's interest scoring (design doc section 5.2): the
// band each dirty record of a client's frame goes in, by priority hint,
// distance, facing, age and the interest cull, and a branch event's copies
// and deadline. Pure: clienttrack.cpp reads the settings and the view,
// counts what the classifier returns, and drains the bands.
//
// Every dirty entity is placed in one of SNAPCL_TIERS bands, and the bands
// are drained in order until the budget runs out. Bands rather than a sort,
// because this runs per client per tic and the extra precision buys
// nothing. The aging term makes it safe: anything unsent for long enough
// climbs to the top band wherever it is, so the worst-case refresh interval
// is bounded however the rest of the scoring behaves.
//
//-----------------------------------------------------------------------------

#ifndef __SNAP_INTEREST_COMPUTE_H__
#define __SNAP_INTEREST_COMPUTE_H__

#include "basictypes.h"
#include "doomdef.h"		// TICRATE
#include "features/net-snapshot/format.h"

// The number of priority bands, which the stats consumer sizes from.
#define SNAPCL_TIERS_REPORT		14

// Band 0, past the starvation bound, above the overdue set that can itself
// exceed the budget.
#define SNAPCL_TIERS			SNAPCL_TIERS_REPORT
#define SNAPCL_TIER_OVERDUE		0
#define SNAPCL_TIER_TOP			1		// players, and the aging guard - ONLY
#define SNAPCL_TIER_ENTER		2		// enters, deaths, promoted terminal records

// Projectiles inside fua_snap_missile_dist, on a band of their own with a
// capped share, since sharing a distance band left them frozen between records.
#define SNAPCL_TIER_MISSILE		3

#define SNAPCL_TIER_DIST		4		// distance bands start here
#define SNAPCL_TIER_DISTLAST	10		// far AND behind: the last ranked band
#define SNAPCL_TIER_REFRESH		11		// held records owing POSITION, on the cosmetic clock

// Held records owing only appearance, apart from TIER_REFRESH's owing position,
// which would otherwise wait behind the many.
#define SNAPCL_TIER_ANIMREFRESH	12
#define SNAPCL_TIER_COSMETIC	( SNAPCL_TIERS - 1 )

// Distance rings in map units, the tier's distance bands: 512, 1024, 2048,
// 4096 and 8192.
#define SNAPCL_DIST_RINGS		5

// One bucket per ring above, plus one for "beyond the last ring".
#define SNAPCL_DIST_BUCKETS	(SNAPCL_DIST_RINGS + 1)

// Entities unseen for this long are promoted; the second bound is a hard
// guarantee that nothing is starved.
#define SNAPCL_AGE_PROMOTE		( TICRATE / 2 )			// 0.5s: climb two bands
#define SNAPCL_AGE_FORCE		( TICRATE * 3 / 2 )		// 1.5s: top band, unconditionally

// How long a cosmetic record may be withheld before it is sent anyway.
#define SNAPCL_AGE_COSMETIC		( TICRATE * 2 )

// Settled scheduler parameters.
#define SNAPCL_HOMING_FLOOR		0	// band floor for steering projectiles, from the first distance band
#define SNAPCL_BALLISTIC_FLOOR	1	// and for ballistic ones, which extrapolate and can afford one more
#define SNAPCL_CONE_NEAR		1024	// radius inside which the view cone does not cull
#define SNAPCL_TERMINAL_MAX		12	// terminal-sequence promotions a frame
#define SNAPCL_EVENT_TERMINAL_COPIES	3	// a death event's copies, and the frames between them
#define SNAPCL_EVENT_TERMINAL_SPACING	3

// Frames an event may live, about an ack round trip, a terminal one adding its
// copies' spacing.
#define SNAPCL_EVENT_DEADLINE	4

// Server-side priority hints from the capture, never on the wire.
enum
{
	SNAPPRI_PLAYER = 1 << 0,
	SNAPPRI_MISSILE = 1 << 1,	// MF_MISSILE or MF_SKULLFLY
	SNAPPRI_HOMING = 1 << 2,	// a STEERING projectile (tracer/seeker/bouncer)
								// - cannot be extrapolated client-side, so it
								// most needs a high refresh to avoid snapping

	// A projectile playing out its death states, whose records are appearance
	// only and so must not be withheld as cosmetic.
	SNAPPRI_TERMINAL = 1 << 3,
};

// What a record's classification reads: the record, its hints and mask, the
// client's view, its ages, and the settings in force.
struct SnapTierIn
{
	SDWORD			qx, qy;			// the record's position, whole units
	BYTE			pri;			// SNAPPRI_*
	BYTE			mask;			// the FULL record mask, SNAPGRP_ENTER included
	bool			bView;			// the client has a viewpoint
	fixed_t			viewX, viewY;
	double			viewCos, viewSin;
	unsigned int	age;			// frames since the record was last sent
	unsigned int	refreshAge;		// frames since it was last refreshed
	bool			bFillMode;		// the baseline fill is under way
	bool			bTransition;	// a death this frame
	int				enterGrade;		// fua_snap_entergrade
	int				missileDist;	// fua_snap_missile_dist
	int				cullDist;		// fua_snap_cull_dist
	double			cullHalfCos;	// the view cone's half-angle cosine, -1 for none
	unsigned int	terminalThisFrame;	// terminal promotions already made this frame
};

// What a terminal-sequence record met, for the caller's counts.
enum
{
	SNAP_TERMINAL_NONE,			// not reached: no terminal flag, or classified before
	SNAP_TERMINAL_SEEN,			// reached, carrying no APPEAR
	SNAP_TERMINAL_PROMOTED,		// promoted to ENTER, one of the frame's quota
	SNAP_TERMINAL_CAPPED,		// wanted promotion past the frame's quota
};

// The census observations beside the band; none of them changes it.
struct SnapTierOut
{
	bool			bForced;		// the starvation guard placed it
	bool			bCosmetic;		// withheld on the cosmetic clock
	bool			bCulled;		// far but in view, on the last ranked band
	unsigned int	distBucket;		// its ring, SNAPCL_DIST_BUCKETS - 1 beyond the last
	bool			bInFront;		// the sign of the view dot
	int				terminal;		// SNAP_TERMINAL_*
};

namespace zx { namespace netsnapshot {

// The held band for a record on the refresh clock: REFRESH when it owes
// position, ANIMREFRESH when only appearance.
unsigned int ComputeRefreshBand(BYTE mask);

// The band a dirty record goes in.
unsigned int ComputeTier(const SnapTierIn& in, SnapTierOut& out);

// A branch event's copies, the frames between them and its deadline:
// terminal events on SNAPCL_EVENT_TERMINAL_COPIES and _SPACING, the rest on
// `eventCopies` (fua_snap_event_copies) one frame apart, clamped to 1-4 copies
// and 1-5 frames.
void ComputeEventDelivery(bool bTerminal, int eventCopies, unsigned int& copies,
	unsigned int& spacing, unsigned int& deadline);

// The own-missile census's bucket for a wait in tics: 0, 1, 2, 3-4, 5-8, 9+.
unsigned int ComputeOwnEnterBucket(int wait);

// A refresh band's quota for one client, with the clamp line that goes with it.
struct SnapBandQuota
{
	unsigned int	quota;		// records the band may spend this frame
	unsigned int	wanted;		// the quota before the ceiling cut it, 0 when it did not
	bool			bWarn;		// this client has just become clamped
};

// The population cycled in `interval` tics and cut to `ceiling` (0 none); `lastWanted` is
// one client's own clamp memory, so no client silences or repeats another's warning.
SnapBandQuota ComputeBandQuota(unsigned int population, unsigned int interval,
	unsigned int ceiling, unsigned int& lastWanted);

}} // namespace zx::netsnapshot

#endif // __SNAP_INTEREST_COMPUTE_H__
