// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: sample_compute.cpp
//
// Description: The client's sampler. See sample_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>

#include "features/net-snapshot/computation/sample_compute.h"

using namespace zx::netsnapshot;

void zx::netsnapshot::ComputePoseFromState(const SnapEntityState& st, SnapGhostPose& out)
{
	out.x = DequantPos(st.qx);
	out.y = DequantPos(st.qy);
	out.z = DequantPos(st.qz);
	out.vx = DequantVel(st.qvx);
	out.vy = DequantVel(st.qvy);
	out.vz = DequantVel(st.qvz);
	out.angle = DequantAngle(st.qangle);
	out.pitch = DequantPitch(st.qpitch);
}

// The clock model's date for a game tic.
static double sample_Date(const SnapClockModel& clock, int tic)
{
	return ComputeClockDate(clock.baseMS, clock.intercept, clock.slope, clock.baseTic, tic);
}

int zx::netsnapshot::ComputeAheadPose(const SnapSampleGhost& g, const SnapClockModel& clock,
	unsigned int renderMS, SnapGhostPose& out)
{
	const double now = (double)renderMS;
	unsigned int iNew = g.head, iOld = g.head;
	double dNew = sample_Date(clock, g.hist[iNew].posTic);
	for (unsigned int k = 1; k < g.count; ++k)
	{
		iOld = (iOld + CLENT_GHOST_HISTORY - 1) & CLENT_GHOST_HISTORY_MASK;
		if (g.hist[iOld].posTic >= g.hist[iNew].posTic)
			continue;		// the same position, carried by a later record
		const double dOld = sample_Date(clock, g.hist[iOld].posTic);
		if (dOld > now)
		{
			iNew = iOld;	// also after now: the pair is further back
			dNew = dOld;
			continue;
		}

		const SnapEntityState& a = g.hist[iOld].state;
		const SnapEntityState& b = g.hist[iNew].state;
		const double ap[3] = { (double)DequantPos(a.qx), (double)DequantPos(a.qy),
			(double)DequantPos(a.qz) };
		const double bp[3] = { (double)DequantPos(b.qx), (double)DequantPos(b.qy),
			(double)DequantPos(b.qz) };
		const double av[3] = { (double)DequantVel(a.qvx), (double)DequantVel(a.qvy),
			(double)DequantVel(a.qvz) };
		const double bv[3] = { (double)DequantVel(b.qvx), (double)DequantVel(b.qvy),
			(double)DequantVel(b.qvz) };
		const double span = (double)(g.hist[iNew].posTic - g.hist[iOld].posTic);
		const double dx = bp[0] - ap[0], dy = bp[1] - ap[1], dz = bp[2] - ap[2];
		const double va = sqrt(av[0] * av[0] + av[1] * av[1] + av[2] * av[2]);
		const double vb = sqrt(bv[0] * bv[0] + bv[1] * bv[1] + bv[2] * bv[2]);
		if (ComputeFlightJoins(sqrt(dx * dx + dy * dy + dz * dz) / (double)FRACUNIT,
			va / (double)FRACUNIT, vb / (double)FRACUNIT, span) == false)
		{
			return SNAP_AHEAD_JUMP;
		}
		// Through both velocities: a gravity class is bridged up to
		// SNAP_BALLISTIC_MAX_TICS, and a straight line cuts across its arc.
		const double f = ComputeAheadFraction(dOld, dNew, now);
		out.x = (fixed_t)ComputeHermite(ap[0], av[0], bp[0], bv[0], span, f);
		out.y = (fixed_t)ComputeHermite(ap[1], av[1], bp[1], bv[1], span, f);
		out.z = (fixed_t)ComputeHermite(ap[2], av[2], bp[2], bv[2], span, f);
		return SNAP_AHEAD_INTERP;
	}
	return SNAP_AHEAD_FIRST;
}

SnapSampleOut zx::netsnapshot::ComputeSample(const SnapSampleGhost& g, const SnapSampleContext& c,
	unsigned int renderMS, SnapGhostPose& out)
{
	SnapSampleOut o = { SNAP_SAMPLE_ABSENT, SNAP_AHEAD_NONE, false, SNAP_LAW_NONE, 0.0,
		false, false, false };

	// The newest state, extrapolated where motion is predictable.
	const SnapEntityState& newest = g.hist[g.head].state;
	const unsigned int newestMS = g.hist[g.head].wallMS;
	// The ages' origins, dated by the clock model when on and trusted, a date
	// past renderMS holding the ghost there.
	const unsigned int newestAt = g.newestAt, posAt = g.posAt;
	const bool bDated = g.bDated;

	out.spriteframe = newest.spriteframe;
	out.renderstate = newest.renderstate;
	out.misc = newest.misc;
	out.effects = newest.effects;
	out.tics = newest.qtics;
	out.stateIdx = newest.qstate;
	out.eventSeq = g.eventSeq;
	out.extrapolated = false;
	out.dated = bDated;
	out.exCapMS = 0;

	// How stale this ghost is, which local animation is judged by.
	out.ageMS = (renderMS > newestAt) ? (renderMS - newestAt) : 0;
	out.arrivalAgeMS = (renderMS > newestMS) ? (renderMS - newestMS) : 0;

	// The same age in tics, by subtraction, for local animation.
	out.ageTics = 0;
	if (c.bHaveServerTic && (c.newestServerTic > g.hist[g.head].serverTic))
		out.ageTics = (unsigned int)(c.newestServerTic - g.hist[g.head].serverTic);
	out.appearAgeTics = 0;
	if (c.bHaveServerTic && (c.newestGameTic > g.hist[g.head].appearTic))
		out.appearAgeTics = (unsigned int)(c.newestGameTic - g.hist[g.head].appearTic);
	out.serverTic = g.hist[g.head].serverTic;
	out.posGapTics = 1;
	if (g.count >= 2)
	{
		const unsigned int iPrevPos = (g.head + CLENT_GHOST_HISTORY - 1) & CLENT_GHOST_HISTORY_MASK;
		const int gap = g.hist[g.head].posTic - g.hist[iPrevPos].posTic;
		if (gap > 1)
			out.posGapTics = (gap > 255) ? 255u : (unsigned int)gap;
	}

	// Presence: a ghost is not drawn before the moment it entered.
	if (renderMS < g.enterMS)
		return o;

	// One sample: only the exits reading the wire velocity run, the rest
	// hold (heldnew).
	const bool bOneSample = (g.count < 2);

	// Velocity from the last two samples, since a walker reports none.
	double vx = 0.0, vy = 0.0, vz = 0.0;
	if (bOneSample == false)
	{
		const unsigned int iPrev = (g.head + CLENT_GHOST_HISTORY - 1) & CLENT_GHOST_HISTORY_MASK;
		const SnapEntityState & prev = g.hist[iPrev].state;
		const unsigned int prevMS = g.hist[iPrev].wallMS;
		if (newestMS > prevMS)
		{
			const double spanTics = (double)(newestMS - prevMS) * (double)TICRATE / 1000.0;
			vx = ((double)DequantPos(newest.qx) - (double)DequantPos(prev.qx)) / spanTics;
			vy = ((double)DequantPos(newest.qy) - (double)DequantPos(prev.qy)) / spanTics;
			vz = ((double)DequantPos(newest.qz) - (double)DequantPos(prev.qz)) / spanTics;

		}
	}

	const unsigned int ageMS = out.ageMS;

	// The age of the position, which a withheld group sets apart from ageMS.
	const unsigned int posAgeMS = (renderMS > posAt) ? (renderMS - posAt) : 0;

	// Ballistic projectiles fly on their velocity, up to the cap.
	if (IsBallistic(newest.misc))
	{
		unsigned int dtMS = posAgeMS;

		// A position dated after now has not happened yet on the model's
		// timeline; drawn at once and then held, the ghost jumps and stalls.
		if (bDated && (posAt > renderMS))
		{
			ComputePoseFromState(newest, out);
			o.ahead = ComputeAheadPose(g, *c.clock, renderMS, out);
			if (o.ahead == SNAP_AHEAD_INTERP)
			{
				// On a path it took, so there is nothing to sweep (exCapMS 0); it
				// stays extrapolated for the smoothing and the floor clamp.
				out.extrapolated = true;
				out.ox = out.x;
				out.oy = out.y;
				out.oz = out.z;
				out.exAccelZ = 0.0;
				out.exCapMS = 0;
				o.exit = SNAP_SAMPLE_AHEAD;
				return o;
			}
		}

		const unsigned int capMS = c.missileCapMS;
		if (dtMS > capMS)
		{
			dtMS = capMS;
			o.bCapped = true;	// frozen at the cap: this is a visible halt
		}

		const double tics = (double)dtMS * (double)TICRATE / 1000.0;

		// Shared with the server's position suppression, which predicts it.
		double ex, ey, ez;
		// The measured acceleration, and zero for a constant-velocity missile
		// because nothing ever set it.
		double useAccel = IsGravBallistic(newest.misc) ? g.accelZ : 0.0;
		bool bLaw = false;
		if (IsGravBallistic(newest.misc))
		{
			// The law the server bridged it by, from the position's tic.
			double lawG = 0.0;
			bool bOldZ = false;
			if ((c.lawGravity != NULL) && c.lawGravity(newest.classId,
				DequantPos(newest.qx), DequantPos(newest.qy), lawG, bOldZ))
			{
				const double vz0 = (double)DequantVel(newest.qvz)
					- lawG * (double)(g.hist[g.head].posTic - g.velZTic);
				ExtrapLaw(newest, vz0, tics, lawG, bOldZ, ex, ey, ez);
				bLaw = true;
				o.law = SNAP_LAW_USED;
				o.lawG = lawG;
				useAccel = -lawG;
			}
			else
				o.law = SNAP_LAW_NOFN;
		}
		if (bLaw == false)
			ExtrapBallistic(newest, tics, ex, ey, ez, useAccel);
		ComputePoseFromState(newest, out);
		out.x = (fixed_t)ex;
		out.y = (fixed_t)ey;
		out.z = (fixed_t)ez;
		out.extrapolated = true;
		out.ox = DequantPos(newest.qx);
		out.oy = DequantPos(newest.qy);
		out.oz = DequantPos(newest.qz);
		out.exAccelZ = useAccel;
		out.exCapMS = capMS;
		o.exit = SNAP_SAMPLE_EXTRAP;
		return o;
	}

	// A ghost with a missile bit and a wire velocity is carried along it for at
	// most CLENT_VELEXTRAP_MS from its newest record, and a body is held.
	if (g.bOther && ((newest.qvx | newest.qvy | newest.qvz) != 0)
		&& MissileFlagged(newest.misc))
	{
		const unsigned int capMS = CLENT_VELEXTRAP_MS;
		unsigned int dtMS = ageMS;
		if (dtMS > capMS)
		{
			dtMS = capMS;
			o.bVelCapped = true;
		}
		const double tics = (double)dtMS * (double)TICRATE / 1000.0;

		// The measured acceleration, only while moving in Z, so a ghost
		// sliding on a floor is not pulled into it.
		const double useAccel = (newest.qvz != 0) ? g.accelZ : 0.0;
		double ex, ey, ez;
		ExtrapBallistic(newest, tics, ex, ey, ez, useAccel);
		ComputePoseFromState(newest, out);
		// >=: a late x/y group stamps posMS past the head's arrival.
		if (g.hist[g.head].posMS >= g.hist[g.head].wallMS)
		{
			out.x = (fixed_t)ex;
			out.y = (fixed_t)ey;
			out.ox = DequantPos(newest.qx);
			out.oy = DequantPos(newest.qy);
			out.oz = DequantPos(newest.qz);
			out.exAccelZ = useAccel;
			out.exCapMS = capMS;
		}
		else
		{
			// x/y from the record that carried them over the x/y clock, clamped
			// per axis to that record's rounding cell.
			unsigned int xyMS = posAgeMS;
			if (xyMS > capMS)
				xyMS = capMS;
			const double xyTics = (double)xyMS * (double)TICRATE / 1000.0;
			const double half = (double)(FRACUNIT / 2);
			const double cx = (double)DequantPos(newest.qx);
			const double cy = (double)DequantPos(newest.qy);
			double fx = cx + (double)DequantVel(newest.qvx) * xyTics;
			double fy = cy + (double)DequantVel(newest.qvy) * xyTics;
			bool bEdge = false;
			if (fx > cx + half)			{ fx = cx + half; bEdge = true; }
			else if (fx < cx - half)	{ fx = cx - half; bEdge = true; }
			if (fy > cy + half)			{ fy = cy + half; bEdge = true; }
			else if (fy < cy - half)	{ fy = cy - half; bEdge = true; }
			out.x = (fixed_t)fx;
			out.y = (fixed_t)fy;
			o.bVelCell = true;
			o.bVelCellEdge = bEdge;
		}
		out.z = (fixed_t)ez;
		out.extrapolated = true;
		o.exit = SNAP_SAMPLE_EXTRAPVEL;
		return o;
	}

	if (bOneSample)
	{
		ComputePoseFromState(newest, out);
		o.exit = SNAP_SAMPLE_HELDNEW;
		return o;
	}

	// Everything else holds at its newest record, moving ones counted apart.
	o.exit = ((vx != 0.0) || (vy != 0.0) || (vz != 0.0)) ? SNAP_SAMPLE_HELDMOVE : SNAP_SAMPLE_HELD;
	ComputePoseFromState(newest, out);
	return o;
}

double zx::netsnapshot::ComputeAheadFraction(double olderDate, double newerDate, double now)
{
	if (newerDate <= olderDate)
		return 1.0;
	const double f = (now - olderDate) / (newerDate - olderDate);
	return (f < 0.0) ? 0.0 : ((f > 1.0) ? 1.0 : f);
}

double zx::netsnapshot::ComputeHermite(double p0, double v0, double p1, double v1, double spanTics, double f)
{
	const double f2 = f * f, f3 = f2 * f;
	return (2.0 * f3 - 3.0 * f2 + 1.0) * p0 + (f3 - 2.0 * f2 + f) * spanTics * v0
		+ (3.0 * f2 - 2.0 * f3) * p1 + (f3 - f2) * spanTics * v1;
}

bool zx::netsnapshot::ComputeFlightJoins(double dist, double speedA, double speedB, double spanTics)
{
	const double fast = (speedA > speedB) ? speedA : speedB;
	return dist <= (2.0 * fast * spanTics + 32.0);
}

int zx::netsnapshot::ComputeTicBucket(int tics)
{
	return (tics < 4) ? 0 : (tics < 9) ? 1 : (tics < 18) ? 2 : (tics < 36) ? 3 : 4;
}
