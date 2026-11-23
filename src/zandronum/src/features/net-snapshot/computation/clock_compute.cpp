//-----------------------------------------------------------------------------
//
// Copyright (C) 2026 Kaori Ishida
//
// Filename: clock_compute.cpp
//
// Description: The client's model of the server clock. See
// clock_compute.h.
//
//-----------------------------------------------------------------------------

#include <math.h>

#include "features/net-snapshot/computation/clock_compute.h"

using namespace zx::netsnapshot;

void zx::netsnapshot::ComputeClockReset(SnapClockModel& m)
{
	m.count = 0;
	m.head = 0;
	m.haveBase = false;
	m.fitValid = false;
	m.meanAbsResid = 0.0;
	m.skipRun = 0;
	m.haveLast = false;
	m.slope = 1000.0 / TICRATE;
	m.intercept = 0.0;
	m.accepted = 0;
	m.windowResid = 0.0;
}

void zx::netsnapshot::ComputeClockRefit(SnapClockModel& m)
{
	if (m.count < 2)
		return;

	double sx = 0, sy = 0, sxx = 0, sxy = 0;
	for (unsigned int i = 0; i < m.count; ++i)
	{
		sx += m.tic[i];
		sy += m.ms[i];
		sxx += m.tic[i] * m.tic[i];
		sxy += m.tic[i] * m.ms[i];
	}
	const double n = (double)m.count;
	const double denom = n * sxx - sx * sx;
	if (fabs(denom) < 1e-9)
		return;
	m.slope = (n * sxy - sx * sy) / denom;
	m.intercept = (sy - m.slope * sx) / n;
	m.fitValid = true;

	// The window's own spread, for fua_csnap_age_maxjitter: unlike the reports'
	// mean it does not restart every 5 s.
	double resid = 0.0;
	for (unsigned int i = 0; i < m.count; ++i)
		resid += fabs(m.ms[i] - (m.slope * m.tic[i] + m.intercept));
	m.windowResid = resid / n;
}

// A reset, then this sample as the new base and last.
static void clock_Seed(SnapClockModel& m, int serverTic, unsigned int wallMS)
{
	ComputeClockReset(m);
	m.baseTic = serverTic;
	m.baseMS = wallMS;
	m.haveBase = true;
	m.lastTic = serverTic;
	m.lastMS = wallMS;
	m.haveLast = true;
}

SnapClockSampled zx::netsnapshot::ComputeClockSample(SnapClockModel& m, int serverTic, unsigned int wallMS)
{
	SnapClockSampled out = { SNAP_CLOCK_ACCEPTED, false, 0.0 };

	// ---- discontinuity detection on consecutive samples ----
	// A real break in the time base, not uneven running, with wall time
	// checked for regression before the unsigned subtraction.
	if (m.haveLast)
	{
		const bool ticWentBack = (serverTic < m.lastTic);
		const bool wallWentBack = (wallMS < m.lastMS);
		const bool hugeGap = (wallMS - m.lastMS) > CLENT_CLOCK_STEP_MS;

		if (ticWentBack || wallWentBack || hugeGap)
		{
			clock_Seed(m, serverTic, wallMS);
			out.outcome = SNAP_CLOCK_RESET;
			return out;
		}
	}

	if (m.haveBase == false)
	{
		m.baseTic = serverTic;
		m.baseMS = wallMS;
		m.haveBase = true;
	}
	m.lastTic = serverTic;
	m.lastMS = wallMS;
	m.haveLast = true;

	const double t = static_cast<double>(serverTic) - static_cast<double>(m.baseTic);
	const double ms = static_cast<double>(wallMS - m.baseMS);

	// ---- adaptive outlier rejection (no reset) ----
	if (m.fitValid && (m.count >= 16))
	{
		const double resid = ms - (m.slope * t + m.intercept);
		const double adaptive = CLENT_CLOCK_OUTLIER_SIGMAS * m.meanAbsResid;
		const double thresh = (adaptive > CLENT_CLOCK_OUTLIER_FLOOR_MS)
			? adaptive : CLENT_CLOCK_OUTLIER_FLOOR_MS;

		if (fabs(resid) > thresh)
		{
			// Rejected, never reset on jitter alone, but a long run re-seeds.
			out.outcome = SNAP_CLOCK_REJECTED;
			if (++m.skipRun >= CLENT_CLOCK_MAX_SKIP_RUN)
			{
				clock_Seed(m, serverTic, wallMS);
				out.outcome = SNAP_CLOCK_RESEEDED;
			}
			return out;
		}
		// Track the spread so the threshold follows the stream's real noise.
		m.meanAbsResid = (0.9 * m.meanAbsResid) + (0.1 * fabs(resid));
		out.bResidual = true;
		out.residual = fabs(resid);
	}
	m.skipRun = 0;

	m.tic[m.head] = t;
	m.ms[m.head] = ms;
	m.head = (m.head + 1) % CLENT_CLOCK_WINDOW;
	if (m.count < CLENT_CLOCK_WINDOW)
		++m.count;
	++m.accepted;

	ComputeClockRefit(m);
	return out;
}

bool zx::netsnapshot::ComputeClockReady(const SnapClockModel& m)
{
	return m.fitValid && (m.count >= CLENT_CLOCK_MIN_READY);
}

double zx::netsnapshot::ComputeClockMsPerTic(const SnapClockModel& m)
{
	return m.fitValid ? m.slope : (1000.0 / TICRATE);
}

bool zx::netsnapshot::ComputeClockFeeds(const SnapClockModel& m, int gameTic)
{
	return (m.haveLast == false) || (gameTic > m.lastTic);
}

bool zx::netsnapshot::ComputeClockTrusted(const SnapClockModel& m)
{
	return m.fitValid && (m.accepted >= CLENT_CLOCK_TRUST_SAMPLES);
}

bool zx::netsnapshot::ComputeClockTicAt(const SnapClockModel& m, unsigned int ms, double& tic)
{
	if (ComputeClockTrusted(m) == false)
		return false;
	tic = (double)m.baseTic + ((double)ms - (double)m.baseMS - m.intercept) / m.slope;
	return true;
}

double zx::netsnapshot::ComputeClockDate(unsigned int baseMS, double intercept, double slope,
	int baseTic, int tic)
{
	return (double)baseMS + intercept + slope * (double)(tic - baseTic);
}

int zx::netsnapshot::ComputeAgeSource(bool bTrusted, double windowJitterMS, int maxJitterMS)
{
	if (bTrusted == false)
		return SNAP_AGE_UNTRUSTED;
	if ((maxJitterMS > 0) && (windowJitterMS > (double)maxJitterMS))
		return SNAP_AGE_JITTERY;
	return SNAP_AGE_MODEL;
}
