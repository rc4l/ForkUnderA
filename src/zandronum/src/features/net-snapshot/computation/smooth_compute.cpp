// SPDX-License-Identifier: GPL-3.0-or-later
//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: smooth_compute.cpp
//
// Description: The mirrors' error smoothing. See smooth_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "features/net-snapshot/computation/smooth_compute.h"

using namespace zx::netsnapshot;

bool zx::netsnapshot::ComputeSmoothStepPop(int pop, unsigned int id, int localPlayer,
	int maxPlayers)
{
	return (pop >= 2)
		|| ((pop == 1) && (id >= 1) && (id <= (unsigned int)maxPlayers)
			&& ((int)id != localPlayer + 1));
}

SnapSmoothIn zx::netsnapshot::ComputeSmoothIn(const SnapGhostPose& pose)
{
	SnapSmoothIn in;
	in.x = pose.x; in.y = pose.y; in.z = pose.z;
	in.vx = pose.vx; in.vy = pose.vy; in.vz = pose.vz;
	in.posGapTics = pose.posGapTics;
	in.serverTic = pose.serverTic;
	in.eventSeq = pose.eventSeq;
	in.extrapolated = pose.extrapolated;
	in.kind = IsBallistic(pose.misc) ? SNAP_SMOOTH_BALLISTIC
		: MissileFlagged(pose.misc) ? SNAP_SMOOTH_TURNING : SNAP_SMOOTH_BODY;
	in.dated = pose.dated;
	return in;
}

// Off course, snaps included: a moving ghost's drawn step against its wire
// velocity, past tan(15) or tan(45) sideways per unit forward, which any step
// back is, with over 0.05 u of it off the velocity's line.
static void smooth_OffCourse(SnapSmoothSplit & cell, const SnapSmoothIn & pose,
	double sx, double sy, double sz)
{
	const double U = 65536.0;
	const double vx = (double)pose.vx, vy = (double)pose.vy, vz = (double)pose.vz;
	const double vl = sqrt(vx * vx + vy * vy + vz * vz);
	const double sl2 = sx * sx + sy * sy + sz * sz;
	const double along = (sx * vx + sy * vy + sz * vz) / vl;
	const double side2 = sl2 - along * along;
	const double side = (side2 > 0.0) ? sqrt(side2) : 0.0;
	const double off = (along >= 0.0) ? side : sqrt(sl2);
	if (off > 0.05 * U)
	{
		if (side > along * 0.2679491924311227)
			++cell.off15;
		if (side > along)
			++cell.off45;
	}
}

void zx::netsnapshot::ComputeSmooth(SnapSmooth & s, const SnapSmoothIn & pose,
	const SnapSmoothCfg & cfg, unsigned int nowMS, SnapSmoothStats & st,
	fixed_t & outX, fixed_t & outY, fixed_t & outZ)
{
	// The target, copied: the caller may pass pose.x/y/z as the outputs.
	const fixed_t tgtX = pose.x, tgtY = pose.y, tgtZ = pose.z;
	outX = tgtX; outY = tgtY; outZ = tgtZ;

	const bool bMoving = (pose.vx != 0) || (pose.vy != 0) || (pose.vz != 0);
	const bool bOffset = s.valid && ((s.ox != 0.0f) || (s.oy != 0.0f) || (s.oz != 0.0f)
		|| (s.kx != 0.0f) || (s.ky != 0.0f) || (s.kz != 0.0f));

	// The population and threshold this frame, an extrapolated ghost keeping
	// its rules until its offset settles.
	const unsigned int gap = (pose.posGapTics > 4) ? 4u : pose.posGapTics;
	const bool bStepPop = cfg.bStepPop;
	const bool bExtrapFam = pose.extrapolated || (s.extrapFam && bOffset);
	const int pop = bExtrapFam ? 1 : 0;
	double limit = 0.0;
	if (bExtrapFam)
		limit = (double)((pose.kind == SNAP_SMOOTH_BALLISTIC) ? cfg.missileLimit : cfg.extrapLimit);
	else if (bStepPop && (cfg.stepLimit > 0))
		limit = (double)cfg.stepLimit * (double)gap;

	// Extrapolated with the threshold at 0 and nothing owed: drawn on the
	// target as below, but through the fold so the census still counts.
	const bool bCountOnly = bExtrapFam && (limit <= 0.0) && (bOffset == false);
	SnapSmoothSplit& cell = st.ex[pose.kind][pose.dated ? 1 : 0];
	if (((limit <= 0.0) && (bCountOnly == false)) || (s.valid == false) || cfg.bInReset)
	{
		// Unsmoothed and already at rest (most mirrors, every frame): track
		// the target without clearing the rest.
		if (s.valid && (bOffset == false))
		{
			s.tx = tgtX; s.ty = tgtY; s.tz = tgtZ;
			s.bx0 = s.bx1 = (float)tgtX; s.by0 = s.by1 = (float)tgtY;
			s.ms = nowMS;
			s.serverTic = pose.serverTic;
			s.eventSeq = pose.eventSeq;
			s.extrap = pose.extrapolated;
			s.extrapFam = pose.extrapolated;
			s.haveRate = false;
			return;
		}
		// An extrapolated ghost owing an offset when its threshold fell to 0
		// drops it in a frame, a snap of its new kind.
		if (bOffset && bExtrapFam && (cfg.bInReset == false))
		{
			const double U = 65536.0;
			const double ox = (double)s.ox + s.kx, oy = (double)s.oy + s.ky;
			const double oz = (double)s.oz + s.kz;
			const double size = sqrt(ox * ox + oy * oy + oz * oz) / U;
			const double sx = (double)tgtX - ((double)s.tx + ox);
			const double sy = (double)tgtY - ((double)s.ty + oy);
			const double sz = (double)tgtZ - ((double)s.tz + oz);
			++st.snaps[1];
			++cell.snaps;
			++cell.snapSize[(size < 0.5) ? 0 : (size < 2.0) ? 1 : 2];
			if (bMoving)
			{
				const double pvx = (double)pose.vx, pvy = (double)pose.vy, pvz = (double)pose.vz;
				const double vl = sqrt(pvx * pvx + pvy * pvy + pvz * pvz);
				if ((sx * pvx + sy * pvy + sz * pvz) / vl < -0.05 * U)
					++cell.snapsBack;
				smooth_OffCourse(cell, pose, sx, sy, sz);
			}
		}
		memset(&s, 0, sizeof(s));
		s.tx = tgtX; s.ty = tgtY; s.tz = tgtZ;
		s.bx0 = s.bx1 = (float)tgtX; s.by0 = s.by1 = (float)tgtY;
		s.ms = nowMS;
		s.serverTic = pose.serverTic;
		s.eventSeq = pose.eventSeq;
		s.extrap = pose.extrapolated;
		s.extrapFam = pose.extrapolated;
		s.valid = true;
		return;
	}

	const double dt = (nowMS > s.ms) ? (double)(nowMS - s.ms) : 0.0;
	const double U = 65536.0;
	bool bSnapped = false;
	const double prevX = (double)s.tx + s.ox + s.kx;
	const double prevY = (double)s.ty + s.oy + s.ky;
	const double prevZ = (double)s.tz + s.oz + s.kz;

	// Decay what was already owed, over the frame just past.
	double om = sqrt(s.ox * s.ox + s.oy * s.oy + s.oz * s.oz);
	if (om > 0.0)
	{
		const double nm = om - s.speed * dt;
		if (nm <= 0.0)
			s.ox = s.oy = s.oz = 0.0f;
		else
		{
			const double k = nm / om;
			s.ox *= k; s.oy *= k; s.oz *= k;
		}
	}
	// The retained part holds while the body moves - decaying it would bring
	// the half-unit drift back - and settles over 200 ms once it stops.
	if (bMoving)
		s.kspeed = 0.0f;
	else
	{
		const double km = sqrt(s.kx * s.kx + s.ky * s.ky + s.kz * s.kz);
		if (km > 0.0)
		{
			if (s.kspeed <= 0.0f)
				s.kspeed = (float)(km / 200.0);
			const double nm = km - s.kspeed * dt;
			if (nm <= 0.0)
				s.kx = s.ky = s.kz = 0.0f;
			else
			{
				const double k = nm / km;
				s.kx *= k; s.ky *= k; s.kz *= k;
			}
		}
	}

	if ((pose.serverTic != s.serverTic) || (pose.eventSeq != s.eventSeq))
	{
		// On a record or event, the old model's position now: its last target
		// plus the motion measured on the last frame without a record.
		double px = (double)s.tx, py = (double)s.ty, pz = (double)s.tz;
		if (s.extrap)
		{
			px += (s.haveRate ? s.rx : s.vx) * dt;
			py += (s.haveRate ? s.ry : s.vy) * dt;
			pz += (s.haveRate ? s.rz : s.vz) * dt;
		}
		const double ddx = (double)tgtX - px, ddy = (double)tgtY - py, ddz = (double)tgtZ - pz;
		const double mag = sqrt(ddx * ddx + ddy * ddy + ddz * ddz) / U;

		om = sqrt((s.ox + s.kx) * (s.ox + s.kx) + (s.oy + s.ky) * (s.oy + s.ky)
			+ (s.oz + s.kz) * (s.oz + s.kz)) / U;
		++st.resid[pop][(om < 0.5) ? 0 : (om < 2.0) ? 1 : (om < 8.0) ? 2 : 3];

		// A fold that moves nothing (a record without position, an event in
		// place) keeps the decay under way rather than restretching it.
		if (mag <= 0.01)
			++st.zero;
		else
		{
			// The whole offset, an extrapolated ghost retaining the part inside
			// the record's cell and folding the excess.
			double tx = s.ox + s.kx - ddx, ty = s.oy + s.ky - ddy, tz = s.oz + s.kz - ddz;
			double kx = 0.0, ky = 0.0, kz = 0.0;
			if (pop == 1)
			{
				const double h = U / 2.0;
				kx = (tx > h) ? h : ((tx < -h) ? -h : tx);
				ky = (ty > h) ? h : ((ty < -h) ? -h : ty);
				kz = (tz > h) ? h : ((tz < -h) ? -h : tz);
				tx -= kx; ty -= ky; tz -= kz;
			}
			const double excess = sqrt(tx * tx + ty * ty + tz * tz) / U;
			if (bCountOnly || (((pop == 1) ? excess : mag) > limit))
			{
				s.ox = s.oy = s.oz = 0.0f;
				s.kx = s.ky = s.kz = 0.0f;
				s.speed = s.kspeed = 0.0f;
				s.bx0 = s.bx1 = (float)tgtX; s.by0 = s.by1 = (float)tgtY;
				bSnapped = true;
				++st.snaps[pop];
				if (pop == 1)
				{
					// The jump drawn, from where the ghost was to its target.
					++cell.snaps;
					++cell.snapSize[(mag < 0.5) ? 0 : (mag < 2.0) ? 1 : 2];
					const double pvx = (double)pose.vx, pvy = (double)pose.vy, pvz = (double)pose.vz;
					const double vl = sqrt(pvx * pvx + pvy * pvy + pvz * pvz);
					const double along = ((double)tgtX - prevX) * pvx
						+ ((double)tgtY - prevY) * pvy + ((double)tgtZ - prevZ) * pvz;
					if (bMoving && (along / vl < -0.05 * U))
						++cell.snapsBack;
				}
			}
			else
			{
				s.ox = (float)tx; s.oy = (float)ty; s.oz = (float)tz;
				s.kx = (float)kx; s.ky = (float)ky; s.kz = (float)kz;
				// Over the newest record's gap, at the actor's speed, what was
				// still owed riding on top.
				const double gapMS = (double)gap * 1000.0 / (double)cfg.ticRate;
				s.speed = (float)(excess * U / gapMS);
				// The drawn position here is unchanged by the fold and lay
				// inside the last segment, so this one starts from it.
				const double fx = (double)tgtX + tx + kx, fy = (double)tgtY + ty + ky;
				s.bx0 = (float)((fx < (double)tgtX) ? fx : (double)tgtX);
				s.bx1 = (float)((fx > (double)tgtX) ? fx : (double)tgtX);
				s.by0 = (float)((fy < (double)tgtY) ? fy : (double)tgtY);
				s.by1 = (float)((fy > (double)tgtY) ? fy : (double)tgtY);
				++st.folds[pop];
				st.foldSum[pop] += mag;
				if (pop == 1)
				{
					++cell.folds;
					cell.foldSum += mag;
				}
			}
		}
		s.serverTic = pose.serverTic;
		s.eventSeq = pose.eventSeq;
		s.haveRate = false;			// a new model: measure it afresh
	}
	else if (s.extrap && pose.extrapolated && (dt > 0.0))
	{
		s.rx = (float)(((double)tgtX - (double)s.tx) / dt);
		s.ry = (float)(((double)tgtY - (double)s.ty) / dt);
		s.rz = (float)(((double)tgtZ - (double)s.tz) / dt);
		s.haveRate = true;
	}
	const double perMS = (double)cfg.ticRate / 1000.0;
	s.vx = (float)((double)pose.vx * perMS); s.vy = (float)((double)pose.vy * perMS);
	s.vz = (float)((double)pose.vz * perMS);

	const double nx = (double)tgtX + s.ox + s.kx;
	const double ny = (double)tgtY + s.oy + s.ky;
	const double nz = (double)tgtZ + s.oz + s.kz;
	if ((s.ox != 0.0f) || (s.oy != 0.0f) || (s.oz != 0.0f)
		|| (s.kx != 0.0f) || (s.ky != 0.0f) || (s.kz != 0.0f))
	{
		++st.offFrames;
		if ((pop == 0) && ((nx < s.bx0 - U) || (nx > s.bx1 + U)
			|| (ny < s.by0 - U) || (ny > s.by1 + U)))
			++st.past;
	}
	else
	{
		s.bx0 = s.bx1 = (float)tgtX;
		s.by0 = s.by1 = (float)tgtY;
	}
	if ((pop == 1) && bMoving)
		smooth_OffCourse(cell, pose, nx - prevX, ny - prevY, nz - prevZ);
	// The wiggle as seen, drawn against the wire velocity, snaps apart.
	if ((pop == 1) && bMoving && (bSnapped == false))
	{
		const double vx = (double)pose.vx, vy = (double)pose.vy, vz = (double)pose.vz;
		const double vl = sqrt(vx * vx + vy * vy + vz * vz);
		++st.movingFrames;
		++cell.moving;
		if (((nx - prevX) * vx + (ny - prevY) * vy + (nz - prevZ) * vz) / vl < -0.05 * U)
		{
			++st.reversed;
			++cell.reversed;
		}
	}
	s.tx = tgtX; s.ty = tgtY; s.tz = tgtZ;
	s.ms = nowMS;
	s.extrap = pose.extrapolated;
	s.extrapFam = bExtrapFam;
	outX = (fixed_t)nx; outY = (fixed_t)ny; outZ = (fixed_t)nz;
}

bool zx::netsnapshot::ComputeSmoothCellText(const SnapSmoothStats & st, unsigned int kind,
	unsigned int dated, const char * unit, char * text, unsigned int size)
{
	static const char * const kinds[SNAP_SMOOTH_KINDS] = { "body", "ballistic", "turning" };
	if (size > 0)
		text[0] = 0;
	if ((kind >= SNAP_SMOOTH_KINDS) || (dated > 1) || (size == 0))
		return false;
	const SnapSmoothSplit& c = st.ex[kind][dated];
	if ((c.folds | c.snaps | c.moving) == 0)
		return false;
	snprintf(text, size, "%s/%s %u folds (mean %.2f u), %u snaps (%u back; by size %u <0.5 u,"
		" %u <2 u, %u 2+ u), REVERSED %u of %u moving %s, off course %u past 15 deg and %u past"
		" 45 deg of %u moving %s and snaps", kinds[kind], dated ? "model" : "arrival", c.folds,
		(c.folds > 0) ? c.foldSum / c.folds : 0.0, c.snaps, c.snapsBack, c.snapSize[0],
		c.snapSize[1], c.snapSize[2], c.reversed, c.moving, unit, c.off15, c.off45,
		c.moving + c.snaps, unit);
	return true;
}

bool zx::netsnapshot::ComputeSmoothSlides(const SnapSmooth & s, const SnapSmoothIn & pose,
	const SnapSmoothCfg & cfg, bool bRawWalk)
{
	if (pose.extrapolated || s.extrapFam)
		return false;
	if (bRawWalk && ((pose.vx | pose.vy | pose.vz) == 0))
		return false;
	return (cfg.bStepPop == false) || (cfg.stepLimit <= 0);
}

bool zx::netsnapshot::ComputeAnchorStart(const fixed_t end[3], const double here[3],
	fixed_t frac, fixed_t start[3])
{
	for (unsigned int a = 0; a < 3; ++a)
		start[a] = end[a];

	const double f = (double)frac / (double)FRACUNIT;
	if ((1.0 - f) < (1.0 / 16.0))
		return false;

	// As an offset from the end, rounded, so a fold that lands on the end gives the
	// end exactly.
	for (unsigned int a = 0; a < 3; ++a)
		start[a] = end[a] + (fixed_t)floor((here[a] - (double)end[a]) / (1.0 - f) + 0.5);
	return true;
}

void zx::netsnapshot::ComputeSlideFrame(bool bSlides, bool bNewTic, const fixed_t pose[3],
	unsigned int gapTics, fixed_t pos[3], fixed_t prev[3])
{
	if (bSlides == false)
	{
		for (unsigned int a = 0; a < 3; ++a)
			pos[a] = prev[a] = pose[a];
		return;
	}
	if (bNewTic == false)
		return;
	const unsigned int gap = (gapTics < 1) ? 1u : ((gapTics > 4) ? 4u : gapTics);
	const double lim = (double)SNAP_SLIDE_SNAP_UNITS * (double)gap * 65536.0;
	double d2 = 0.0;
	for (unsigned int a = 0; a < 3; ++a)
	{
		const double d = (double)pose[a] - (double)pos[a];
		d2 += d * d;
	}
	const bool bSnap = (d2 > lim * lim);
	for (unsigned int a = 0; a < 3; ++a)
	{
		prev[a] = bSnap ? pose[a] : pos[a];
		pos[a] = pose[a];
	}
}
