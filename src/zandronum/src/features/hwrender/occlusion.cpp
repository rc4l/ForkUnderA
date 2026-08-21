// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/occlusion.h"

#include "doomtype.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_utility.h"   // viewx, viewy, viewz, R_PointToAngle
#include "actor.h"
#include "tables.h"
#include "p_3dfloors.h"   // F3DFloor, to find the control sectors
#include "tarray.h"
#include "stats.h"        // cycle_t, to price the build against what it saves

#include <math.h>
#include <stdlib.h>       // qsort

namespace zx { namespace hwrender {

namespace {

// [rc4l] 2048 buckets is about a tenth of a degree, finer than a distant actor is wide.
//
// Indexed by the top bits of an angle_t, so the circle is covered and wraparound is free: a blocker
// straddling angle zero writes both halves without knowing it did.
const int kBucketBits = 11;
const int kBuckets = 1 << kBucketBits;
const int kAngleShift = 32 - kBucketBits;

const float kFarAway = 1.0e30f;

// [rc4l] Two limits per bucket, which is the shape Doom's own renderer used.
//
// Not "the nearest wall". Doom clipped each column against a FLOOR limit and a CEILING limit, and
// that is the right decomposition here for the same reason it was there: it is what the geometry
// does. The step or terrace in front of you hides everything below a line; the lintel or the lower
// ceiling beyond hides everything above one. A single band per bucket expresses neither, and the
// version that tried culled a twentieth of what it should have.
//
// `lo` hides everything BELOW its slope, `hi` everything ABOVE, each only beyond its own distance.
struct Blocker
{
	float depth;   // nothing nearer than this is affected
	float slope;   // (limit height - eye) / depth: how far up the view the limit reaches
};
Blocker g_lo[kBuckets], g_hi[kBuckets];

bool   g_active = false;
int    g_used = 0, g_candidates = 0;
// The build's own clock. Inferring it from the frame time did not work: the cost and the saving are
// both a fraction of a millisecond, and this machine's frame-to-frame noise is larger than either.
double g_buildMs = 0.0;

// Sectors that exist only to define a 3D floor. Real geometry at real coordinates that is never
// drawn, so it must never occlude -- left in, the buffer blocks sight with the walls of control
// sectors parked out in the void, and takes the torches off the towers of Sunder MAP16.
TArray<bool> g_isControl;
const line_t *g_controlFor = NULL;

// [rc4l] The lines worth even LOOKING at, ranked once when the level loads.
//
// Generating blockers from every linedef is O(map): 56,000 lines and 8.7 ms a frame on Sunder MAP16,
// against sprite work of 1.4 ms. But most of a map can never hide much, and whether it can does not
// depend on where the camera is -- a line is a big blocker if it is long AND the wall or step it
// carries is tall. So they are ranked once, camera-free, and a frame walks as much of the front of
// that list as it can afford. 1024 lines costs about 0.11 ms and finds most of what all 56,000 do.
TArray<int> g_ranked;

struct RankEntry { int line; float size; };

int BiggestFirst(const void *a, const void *b)
{
	const float sa = ((const RankEntry *)a)->size, sb = ((const RankEntry *)b)->size;
	return (sa > sb) ? -1 : (sa < sb) ? 1 : 0;
}

// How much wall a line carries, in square map units: blocking height times length.
float StaticBlockerSize(const line_t &ln)
{
	const sector_t *fs = ln.frontsector;
	if (fs == NULL) return 0.f;
	const float dx = FIXED2FLOAT(ln.v2->x - ln.v1->x), dy = FIXED2FLOAT(ln.v2->y - ln.v1->y);
	const float len = sqrtf(dx * dx + dy * dy);
	if (len <= 0.f) return 0.f;

	const float fc = FIXED2FLOAT(fs->ceilingplane.ZatPoint(ln.v1));
	const float ff = FIXED2FLOAT(fs->floorplane.ZatPoint(ln.v1));
	const sector_t *bs = ln.backsector;
	if (ln.sidedef[1] == NULL || bs == NULL) return len * (fc - ff);

	const float bc = FIXED2FLOAT(bs->ceilingplane.ZatPoint(ln.v1));
	const float bf = FIXED2FLOAT(bs->floorplane.ZatPoint(ln.v1));
	// A two-sided line blocks with its step and with its lintel; the bigger of the two ranks it.
	const float step = (ff > bf) ? (ff - bf) : (bf - ff);
	const float lint = (fc > bc) ? (fc - bc) : (bc - fc);
	return len * ((step > lint) ? step : lint);
}

void RebuildControlSectors()
{
	g_isControl.Resize((unsigned)numsectors);
	for (int i = 0; i < numsectors; i++) g_isControl[i] = false;
	for (int i = 0; i < numsectors; i++)
	{
		const sector_t &sec = sectors[i];
		if (sec.e == NULL) continue;
		const TArray<F3DFloor *> &ff = sec.e->XFloor.ffloors;
		for (unsigned k = 0; k < ff.Size(); k++)
		{
			if (ff[k] == NULL || ff[k]->model == NULL) continue;
			const int n = (int)(ff[k]->model - sectors);
			if (n >= 0 && n < numsectors) g_isControl[n] = true;
		}
	}
	g_controlFor = lines;
}

void RebuildRanking()
{
	TArray<RankEntry> all;
	all.Grow((unsigned)numlines);
	for (int i = 0; i < numlines; i++)
	{
		const line_t &ln = lines[i];
		if (ln.v1 == NULL || ln.v2 == NULL || ln.frontsector == NULL) continue;
		const int sn = (int)(ln.frontsector - sectors);
		if (sn >= 0 && sn < (int)g_isControl.Size() && g_isControl[sn]) continue;
		RankEntry e;
		e.line = i;
		e.size = StaticBlockerSize(ln);
		if (e.size <= 0.f) continue;
		all.Push(e);
	}
	if (all.Size() > 1) qsort(&all[0], (size_t)all.Size(), sizeof(RankEntry), BiggestFirst);
	g_ranked.Resize(all.Size());
	for (unsigned i = 0; i < all.Size(); i++) g_ranked[i] = all[i].line;
}

// A wall subtends less than half the circle from any point, so the span is whichever way round is
// shorter -- and that also says which endpoint is the start.
inline bool SpanOf(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2, angle_t &outA1, angle_t &outA2)
{
	const angle_t av1 = R_PointToAngle(x1, y1);
	const angle_t av2 = R_PointToAngle(x2, y2);
	const angle_t forward = av2 - av1;
	if (forward == 0) return false;
	if (forward <= ANGLE_180) { outA1 = av1; outA2 = av2; }
	else                      { outA1 = av2; outA2 = av1; }
	return true;
}

inline float DistTo(fixed_t x, fixed_t y)
{
	const float dx = FIXED2FLOAT(x - viewx), dy = FIXED2FLOAT(y - viewy);
	return sqrtf(dx * dx + dy * dy);
}

// [rc4l] Paint one limit across the buckets it covers, keeping whichever hides MORE.
//
// Measured as slope, not as height. Keeping the tallest was the obvious rule and it is wrong in a way
// that showed up as the curve running backwards -- adding blockers REDUCED what was culled. A tall
// wall far away beats a low step underfoot on height while hiding almost nothing, and once it owns
// the bucket the step is gone and the distance test spares everything nearer than the wall. Slope is
// the honest measure of how much of the view a limit covers, and it carries the distance already.
//
// Order-independent, which is what makes the build cheap: there is nothing to sort.
void Paint(float depth, float slope, bool isLo, angle_t a1, angle_t a2)
{
	int b = (int)(a1 >> kAngleShift);
	const int end = (int)(a2 >> kAngleShift);
	for (int guard = 0; guard <= kBuckets; guard++)
	{
		if (isLo)
		{
			Blocker &e = g_lo[b];
			if (slope > e.slope || (slope == e.slope && depth < e.depth))
				{ e.slope = slope; e.depth = depth; }
		}
		else
		{
			Blocker &e = g_hi[b];
			if (slope < e.slope || (slope == e.slope && depth < e.depth))
				{ e.slope = slope; e.depth = depth; }
		}
		if (b == end) break;
		b = (b + 1) & (kBuckets - 1);
	}
}

// [rc4l] The slope has to be taken at whichever END of the wall claims LESS.
//
// A wall crosses the view at every distance between its two ends and one number stands for all of
// them. Using the far end looks conservative and is not: for a limit BELOW the eye -- which every
// floor step is -- height-over-distance is negative, and dividing by the larger distance moves it
// toward zero, so the limit claims to reach further up the view than it does.
inline void PaintLimit(float z, bool isLo, float dNear, float dFar, angle_t a1, angle_t a2)
{
	const float h = z - FIXED2FLOAT(viewz);
	const float sNear = h / dNear, sFar = h / dFar;
	const float slope = isLo ? ((sNear < sFar) ? sNear : sFar) : ((sNear > sFar) ? sNear : sFar);
	Paint(dFar, slope, isLo, a1, a2);
	g_candidates++;
}

} // namespace

void OcclusionBuild(int nLines)
{
	g_active = false;
	g_used = 0;
	g_candidates = 0;
	g_buildMs = 0.0;
	if (nLines <= 0 || numlines <= 0 || lines == NULL) return;

	cycle_t clock;
	clock.Reset();
	clock.Clock();

	if (g_controlFor != lines) { RebuildControlSectors(); RebuildRanking(); }
	if (g_ranked.Size() == 0) { clock.Unclock(); return; }

	for (int i = 0; i < kBuckets; i++)
	{
		g_lo[i].depth = kFarAway; g_lo[i].slope = -kFarAway;
		g_hi[i].depth = kFarAway; g_hi[i].slope =  kFarAway;
	}

	const int want = (nLines < (int)g_ranked.Size()) ? nLines : (int)g_ranked.Size();
	for (int r = 0; r < want; r++)
	{
		const line_t &ln = lines[g_ranked[r]];

		angle_t a1, a2;
		if (!SpanOf(ln.v1->x, ln.v1->y, ln.v2->x, ln.v2->y, a1, a2)) continue;
		const float d1 = DistTo(ln.v1->x, ln.v1->y);
		const float d2 = DistTo(ln.v2->x, ln.v2->y);
		const float dFar = (d1 > d2) ? d1 : d2;
		const float dNear = (d1 < d2) ? d1 : d2;
		if (dNear < 1.f) continue;   // standing on it: no honest slope to take

		const sector_t *fs = ln.frontsector;
		const sector_t *bs = ln.backsector;
		const float fc1 = FIXED2FLOAT(fs->ceilingplane.ZatPoint(ln.v1));
		const float fc2 = FIXED2FLOAT(fs->ceilingplane.ZatPoint(ln.v2));

		if (ln.sidedef[1] == NULL || bs == NULL)
		{
			// A one-sided wall is solid floor to ceiling and the floor beneath it is solid too, so
			// everything below its TOP is hidden. The lower of the two ends keeps that true across
			// the whole span.
			PaintLimit((fc1 < fc2) ? fc1 : fc2, true, dNear, dFar, a1, a2);
			continue;
		}

		const float ff1 = FIXED2FLOAT(fs->floorplane.ZatPoint(ln.v1));
		const float ff2 = FIXED2FLOAT(fs->floorplane.ZatPoint(ln.v2));
		const float bc1 = FIXED2FLOAT(bs->ceilingplane.ZatPoint(ln.v1));
		const float bc2 = FIXED2FLOAT(bs->ceilingplane.ZatPoint(ln.v2));
		const float bf1 = FIXED2FLOAT(bs->floorplane.ZatPoint(ln.v1));
		const float bf2 = FIXED2FLOAT(bs->floorplane.ZatPoint(ln.v2));

		// The step: whatever the two floors do, nothing below the higher of them shows through this
		// line, and the lower of the two ends is what it certainly covers. This is the case a
		// one-sided-only version missed entirely, and in a map built out of terraces and platforms it
		// is most of the occlusion there is.
		const float step1 = (ff1 > bf1) ? ff1 : bf1;
		const float step2 = (ff2 > bf2) ? ff2 : bf2;
		PaintLimit((step1 < step2) ? step1 : step2, true, dNear, dFar, a1, a2);

		// ...and the lintel: nothing above the lower of the two ceilings gets through either.
		const float lint1 = (fc1 < bc1) ? fc1 : bc1;
		const float lint2 = (fc2 < bc2) ? fc2 : bc2;
		PaintLimit((lint1 > lint2) ? lint1 : lint2, false, dNear, dFar, a1, a2);
	}
	g_used = want;
	g_active = true;
	clock.Unclock();
	g_buildMs = clock.TimeMS();
}

bool OcclusionHidden(const AActor *thing)
{
	if (!g_active || thing == NULL) return false;

	const float dx = FIXED2FLOAT(thing->x - viewx), dy = FIXED2FLOAT(thing->y - viewy);
	const float dist = sqrtf(dx * dx + dy * dy);
	const float radius = FIXED2FLOAT(thing->radius);
	// The actor's NEAR edge, because it is visible if any part of it is.
	const float nearEdge = dist - radius;
	if (nearEdge <= 1.f) return false;   // on top of the camera: never claim it is hidden

	// [rc4l] The vertical test is PROJECTIVE, not a comparison of world heights.
	//
	// Asking "is the actor between this wall's floor and its ceiling" is the wrong question, and it
	// answers no almost everywhere: in a map made of terraces the camera and the thing it is looking
	// at are rarely inside the same wall's band, and that version culled 5% where the honest answer
	// was 85%. What decides whether something is hidden is how much of the VIEW the limit covers, so
	// both are reduced to slope from the eye -- height over distance.
	const float aBottom = FIXED2FLOAT(thing->z);
	const float aTop = aBottom + FIXED2FLOAT(thing->height);
	const float eye = FIXED2FLOAT(viewz);
	const float invNear = 1.f / nearEdge;
	const float aSlopeTop = (aTop - eye) * invNear;
	const float aSlopeBottom = (aBottom - eye) * invNear;

	const angle_t centre = R_PointToAngle(thing->x, thing->y);

	// [rc4l] The centre bucket first, and usually last.
	//
	// Most actors are visible, so most calls end here -- and testing the span from one end meant
	// walking it before finding that out. On the eon maps, where there is little to cull, paying the
	// full walk per actor cost more than the culling saved. One bucket answers the common case.
	{
		const int b0 = (int)(centre >> kAngleShift);
		const bool underLo = (nearEdge > g_lo[b0].depth) && (aSlopeTop <= g_lo[b0].slope);
		const bool overHi  = (nearEdge > g_hi[b0].depth) && (aSlopeBottom >= g_hi[b0].slope);
		if (!underLo && !overHi) return false;
	}

	// Only now is the whole width worth checking. The half-angle is the small-angle approximation of
	// atan(radius / near) rounded UP, which is what a conservative test wants: too wide costs a few
	// buckets, too narrow would claim something hidden that is not.
	const float halfF = radius * invNear;
	const angle_t half = (angle_t)(halfF * (float)(ANGLE_180 / M_PI)) + (angle_t)(ANGLE_1 / 2);

	int b = (int)((centre - half) >> kAngleShift);
	const int end = (int)((centre + half) >> kAngleShift);
	for (int guard = 0; guard <= kBuckets; guard++)
	{
		// Hidden in this bucket if it sits entirely under the floor limit, or entirely over the
		// ceiling one. A single bucket that hides it neither way is enough to keep the actor.
		const bool underLo = (nearEdge > g_lo[b].depth) && (aSlopeTop <= g_lo[b].slope);
		const bool overHi  = (nearEdge > g_hi[b].depth) && (aSlopeBottom >= g_hi[b].slope);
		if (!underLo && !overHi) return false;
		if (b == end) break;
		b = (b + 1) & (kBuckets - 1);
	}
	return true;
}

void OcclusionStats(int &linesUsed, int &limitsPainted, double &buildMs)
{
	linesUsed = g_used;
	limitsPainted = g_candidates;
	buildMs = g_buildMs;
}

}} // namespace zx::hwrender
