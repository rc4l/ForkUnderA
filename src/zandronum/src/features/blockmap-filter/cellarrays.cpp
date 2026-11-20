// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/blockmap-filter/cellarrays.h"
#include "features/blockmap-filter/computation/blockfilter_compute.h"
#include "actor.h"
#include "doomdata.h"
#include "m_bbox.h"
#include "p_local.h"
#include "r_defs.h"
#include "r_state.h"

namespace zx { namespace cells {

unsigned int Epoch = 0;

namespace {

std::vector<std::vector<ThingEntry> > Cells;
std::vector<std::vector<LineEntry> > LineCells;
bool LinesBuilt = false;
std::vector<std::vector<SightLineEntry> > SightCells;
bool SightBuilt = false;
bool SightFits = false;

// [Doruk-Aksoy] Every cell's static lines, in the blockmap's order, as `make` copies each.
template <class Entry, class Make>
void BuildFromBlockmap(std::vector<std::vector<Entry> > &out, Make make)
{
	out.assign(Cells.size(), std::vector<Entry>());
	for (size_t cell = 0; cell < out.size(); cell++)
		for (const int *list = blockmaplump + blockmap[cell] + 1; *list != -1; list++)
			out[cell].push_back(make(&lines[*list]));
}

void BuildLines()
{
	BuildFromBlockmap(LineCells, [](line_t *ld) {
		const LineEntry e = { ld->bbox[BOXLEFT].Raw(), ld->bbox[BOXRIGHT].Raw(), ld->bbox[BOXBOTTOM].Raw(), ld->bbox[BOXTOP].Raw(), (void *)ld };
		return e;
	});
	LinesBuilt = true;
}

} // namespace

void Reset(int count)
{
	Cells.assign(count > 0 ? (size_t)count : 0, std::vector<ThingEntry>());
	LineCells.clear();
	LinesBuilt = false;
	SightCells.clear();
	SightBuilt = false;
	Epoch++;
}

void Link(FBlockNode *node, AActor *who, bool multi)
{
	if ((size_t)node->BlockIndex >= Cells.size()) return;
	const ThingEntry e = { who->x.Raw(), who->y.Raw(), who->radius.Raw(), who->GetAttackRadius().Raw(), who, node, multi };
	ComputeCellLink(Cells[node->BlockIndex], e);
	Epoch++;
}

void Unlink(FBlockNode *node)
{
	if ((size_t)node->BlockIndex >= Cells.size()) return;
	ComputeCellUnlink(Cells[node->BlockIndex], node);
	Epoch++;
}

void Refresh(AActor *who)
{
	for (FBlockNode *node = who->BlockNode; node != NULL; node = node->NextBlock)
	{
		if ((size_t)node->BlockIndex < Cells.size())
			ComputeCellRefresh(Cells[node->BlockIndex], node, who->x.Raw(), who->y.Raw(), who->radius.Raw(), who->GetAttackRadius().Raw());
	}
}

const std::vector<ThingEntry> &Things(int cell)
{
	return Cells[(size_t)cell];
}

void BuildStaticLines()
{
	if (!LinesBuilt) BuildLines();
	if (!SightBuilt)
	{
		SightFits = true;
		for (int i = 0; i < numlines && SightFits; i++)
			SightFits = ComputeSightLineFits(lines[i].v1->x.Raw(), lines[i].v1->y.Raw(), lines[i].v2->x.Raw(), lines[i].v2->y.Raw(), lines[i].dx.Raw(), lines[i].dy.Raw());
		if (SightFits)
		{
			BuildFromBlockmap(SightCells, [](line_t *ld) {
				const SightLineEntry e = { (int32_t)ld->v1->x.Raw(), (int32_t)ld->v1->y.Raw(), (int32_t)ld->v2->x.Raw(), (int32_t)ld->v2->y.Raw(), (int32_t)(ld - lines) };
				return e;
			});
		}
		SightBuilt = true;
	}
}

bool SightReady()
{
	if (!SightBuilt) BuildStaticLines();
	return SightFits;
}

const std::vector<SightLineEntry> &SightLines(int cell)
{
	return SightCells[(size_t)cell];
}

bool Missed(const ThingEntry &e, const AActor *mover, int64_t moverX, int64_t moverY)
{
	return ComputeBlockCandidateMissed(e.x, e.y, e.radius, e.attackRadius, moverX, moverY, mover->radius.Raw(), (mover->flags & MF_MISSILE) != 0);
}

LineWalk::LineWalk(const FBoundingBox &box)
{
	// [Doruk-Aksoy] As FBlockLinesIterator's constructor, so the marks it sets are this walk's.
	validcount++;
	maxy = GetSafeBlockY(box.Top() - bmaporgy);
	miny = GetSafeBlockY(box.Bottom() - bmaporgy);
	maxx = GetSafeBlockX(box.Right() - bmaporgx);
	minx = GetSafeBlockX(box.Left() - bmaporgx);
	left = box.Left().Raw();
	right = box.Right().Raw();
	bottom = box.Bottom().Raw();
	top = box.Top().Raw();
	if (!LinesBuilt) BuildLines();
	Start(minx, miny);
}

void LineWalk::Start(int x, int y)
{
	curx = x;
	cury = y;
	cell = (x >= 0 && y >= 0 && x < bmapwidth && y < bmapheight) ? &LineCells[(size_t)(y * bmapwidth + x)] : NULL;
	pos = 0;
}

line_t *LineWalk::Next()
{
	for (;;)
	{
		while (cell != NULL && pos < cell->size())
		{
			const LineEntry &e = (*cell)[pos++];
			if (ComputeBoxMissesLine(e, left, right, bottom, top)) continue;
			line_t *ld = static_cast<line_t *>(e.line);
			if (ld->validcount == validcount) continue;
			ld->validcount = validcount;
			return ld;
		}
		if (++curx > maxx)
		{
			curx = minx;
			if (++cury > maxy) return NULL;
		}
		Start(curx, cury);
	}
}

}} // namespace zx::cells
