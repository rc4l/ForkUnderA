// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/swept-missiles/sweptmove.h"
#include "features/swept-missiles/computation/sweptmove_compute.h"
#include "doomstat.h"
#include "m_bbox.h"
#include "p_local.h"
#include "r_defs.h"
#include "r_state.h"
#include "po_man.h"
#include "g_shared/a_sharedglobal.h"
#include "features/blockmap-filter/cellarrays.h"
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>

extern polyblock_t **PolyBlockMap;

namespace zx { namespace sweptmove {

unsigned int BlockmapSpawns = 0;

namespace {

// [Doruk-Aksoy] The planned steps' raw x, y and z (index 0 is where the mover stands); reused every call.
TArray<int64_t> StepX, StepY, StepZ;

bool CanBeHit(const AActor *t)
{
	return (t->flags & (MF_SOLID | MF_SPECIAL | MF_SHOOTABLE)) || (t->flags6 & MF6_TOUCHY);
}

// [Doruk-Aksoy] The first of steps 1..before-1 whose box a line straddles as PIT_CheckLine judges it, or before.
int FirstStraddle(const line_t *ld, int before, int64_t r)
{
	for (int k = 1; k < before; k++)
	{
		const FBoundingBox box(fixed_t::FromRaw(StepX[k]), fixed_t::FromRaw(StepY[k]), fixed_t::FromRaw(r));
		if (!(box.Right() <= ld->bbox[BOXLEFT] || box.Left() >= ld->bbox[BOXRIGHT] || box.Top() <= ld->bbox[BOXBOTTOM] || box.Bottom() >= ld->bbox[BOXTOP])
			&& box.BoxOnLineSide(ld) == -1)
			return k;
	}
	return before;
}

// [Doruk-Aksoy] How many leading steps are quiet: none meets a thing PIT_CheckThing acts on or a line PIT_CheckLine acts on, leaves the sector or meets its floor or ceiling; stops looking once fewer than two can be.
int RunEnd(AActor *mo, int steps)
{
	sector_t *sec = mo->Sector;
	const int64_t r = mo->radius.Raw();
	int first = ComputeFirstUnclearStep(&StepZ[0], steps, mo->height.Raw(), sec->floorplane.ZatPoint(mo->x, mo->y).Raw(), sec->ceilingplane.ZatPoint(mo->x, mo->y).Raw());

	// [Doruk-Aksoy] The blocks every step's own check would walk, as its iterator picks them.
	int x1 = INT_MAX, x2 = INT_MIN, y1 = INT_MAX, y2 = INT_MIN;
	for (int k = 1; k < first; k++)
	{
		const int lo = GetSafeBlockX(fixed_t::FromRaw(StepX[k] - r) - bmaporgx), hi = GetSafeBlockX(fixed_t::FromRaw(StepX[k] + r) - bmaporgx);
		const int bo = GetSafeBlockY(fixed_t::FromRaw(StepY[k] - r) - bmaporgy), to = GetSafeBlockY(fixed_t::FromRaw(StepY[k] + r) - bmaporgy);
		x1 = std::min(x1, lo); x2 = std::max(x2, hi); y1 = std::min(y1, bo); y2 = std::max(y2, to);
	}
	x1 = std::max(x1, 0); y1 = std::max(y1, 0); x2 = std::min(x2, bmapwidth - 1); y2 = std::min(y2, bmapheight - 1);

	const bool things = P_MoverChecksThings(mo);
	for (int by = y1; first > 2 && by <= y2; by++)
	{
		for (int bx = x1; first > 2 && bx <= x2; bx++)
		{
			const int cell = by * bmapwidth + bx;
			if (things)
			{
				// [Doruk-Aksoy] features/blockmap-filter: distance from the cell's array first, the actor's flags only for those a step meets.
				const std::vector<zx::cells::ThingEntry> &cellThings = zx::cells::Things(cell);
				for (size_t i = 0; first > 2 && i < cellThings.size(); i++)
				{
					const zx::cells::ThingEntry &e = cellThings[i];
					if (e.actor == mo) continue;
					const int k = ComputeFirstStepMeetingThing(&StepX[0], &StepY[0], first, e.x, e.y, e.attackRadius, r);
					if (k < first && CanBeHit(static_cast<const AActor *>(e.actor))) first = k;
				}
			}
			if (r == 0) continue;
			for (const int *list = blockmaplump + blockmap[cell] + 1; first > 2 && *list != -1; list++)
				first = FirstStraddle(&lines[*list], first, r);
			for (const polyblock_t *pl = PolyBlockMap != NULL ? PolyBlockMap[cell] : NULL; first > 2 && pl != NULL; pl = pl->next)
				for (unsigned i = 0; pl->polyobj != NULL && first > 2 && i < pl->polyobj->Linedefs.Size(); i++)
					first = FirstStraddle(pl->polyobj->Linedefs[i], first, r);
		}
	}
	for (int k = 1; first > 2 && k < first; k++)
		if (P_PointInSector(fixed_t::FromRaw(StepX[k]), fixed_t::FromRaw(StepY[k])) != sec) first = k;
	const int quiet = first - 1;
	return quiet;
}

void Plan(AActor *mo, int steps)
{
	StepX.Resize(steps + 1);
	StepY.Resize(steps + 1);
	StepZ.Resize(steps + 1);
	StepX[0] = mo->x.Raw();
	StepY[0] = mo->y.Raw();
	StepZ[0] = mo->z.Raw();
}

} // namespace

bool Sweeps(AActor *mo, bool onWalkPlane)
{
	const MoverAsk m = { (mo->flags & MF_MISSILE) != 0, mo->player != NULL, (mo->flags & MF_NOCLIP) != 0, (mo->flags6 & MF6_STEPMISSILE) != 0,
		(mo->flags2 & MF2_BLASTED) != 0, (mo->flags3 & (MF3_FLOORHUGGER | MF3_CEILINGHUGGER)) != 0, (mo->flags2 & MF2_CANTLEAVEFLOORPIC) != 0,
		onWalkPlane, (i_compatflags & COMPATF_WALLRUN) != 0 };
	if (!ComputeSweepsMover(m) || mo->Sector == NULL) return false;
	const sector_t *sec = mo->Sector;
	const SectorAsk s = { sec->floorplane.a.Raw() == 0 && sec->floorplane.b.Raw() == 0, sec->ceilingplane.a.Raw() == 0 && sec->ceilingplane.b.Raw() == 0,
		sec->e->XFloor.ffloors.Size() != 0, sec->heightsec != NULL, (sec->MoreFlags & SECF_UNDERWATER) != 0 };
	return ComputeSweepsSector(s);
}

int QuietRunEndXY(AActor *mo, fixed_t startx, fixed_t starty, fixed_t xmove, fixed_t ymove, int steps)
{
	Plan(mo, steps);
	for (int k = 1; k <= steps; k++)
	{
		StepX[k] = (startx + Scale(xmove, k, steps)).Raw();
		StepY[k] = (starty + Scale(ymove, k, steps)).Raw();
		StepZ[k] = mo->z.Raw();
	}
	return RunEnd(mo, steps);
}

int QuietRunEndFast(AActor *mo, fixed_t xfrac, fixed_t yfrac, fixed_t zfrac, int count)
{
	Plan(mo, count);
	for (int k = 1; k <= count; k++)
	{
		StepX[k] = StepX[k - 1] + xfrac.Raw();
		StepY[k] = StepY[k - 1] + yfrac.Raw();
		StepZ[k] = StepZ[k - 1] + zfrac.Raw();
	}
	return RunEnd(mo, count);
}

void PlanFast(AActor *p, FastPlan &plan, int from, int count, fixed_t xfrac, fixed_t yfrac, fixed_t zfrac)
{
	const int remaining = count - from;
	if (plan.sweepsLeft <= 0 || !ComputeSweepPays(remaining) || !Sweeps(p, false))
	{
		plan.emulateTo = from;
		plan.loudAt = -1;
		return;
	}
	plan.sweepsLeft--;
	ComputeFastPlan(plan, from, remaining, QuietRunEndFast(p, xfrac, yfrac, zfrac, remaining));
}

bool EmulateQuietFastStep(AFastProjectile *p, FCheckPosition &tm, int &ripcount, int every, fixed_t xfrac, fixed_t yfrac, fixed_t zfrac, fixed_t &oldz)
{
	const RipStep rip = ComputeRipStep(ripcount, every);
	if (rip.clearRipped) tm.LastRipped = NULL;
	// [Doruk-Aksoy] What P_TryMove sets for a box that meets nothing in a plain sector.
	sector_t *sec = p->Sector;
	p->x += xfrac;
	p->y += yfrac;
	if (!(p->flags & MF_NOBLOCKMAP)) zx::cells::Refresh(p);	// [Doruk-Aksoy] features/blockmap-filter: moved without relinking
	p->floorz = p->dropoffz = sec->floorplane.ZatPoint(p->x, p->y);
	p->ceilingz = sec->ceilingplane.ZatPoint(p->x, p->y);
	p->floorpic = sec->GetTexture(sector_t::floor);
	p->ceilingpic = sec->GetTexture(sector_t::ceiling);
	p->floorsector = p->ceilingsector = sec;
	p->STFlags |= STFL_POSITIONCHANGED;
	p->z += zfrac;
	p->UpdateWaterLevel(oldz);
	oldz = p->z;
	if (!rip.effect) return true;
	const unsigned int before = BlockmapSpawns;
	p->Effect();
	return BlockmapSpawns == before;
}

}} // namespace zx::sweptmove
