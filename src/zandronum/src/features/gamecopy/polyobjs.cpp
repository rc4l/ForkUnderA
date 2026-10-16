// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/polyobjs.h"
#include "features/gamecopy/computation/polyblend_compute.h"
#include "features/telemetry/telemetry.h"

#include "po_man.h"
#include "p_local.h"   // [rc4l] po_NumPolyobjs
#include "r_state.h"

#include <memory>

namespace zx { namespace gamecopy {

namespace {

// [rc4l] A copied BSP vertex that lies on a polyobject's line, and how far along it.
struct OnLine
{
	int vert;
	const line_t *line;
	double t;
};

// [rc4l] A subsector's copied BSP and the vertices of it that ride its polyobjects' lines.
struct PolyCopy
{
	FMiniBSP bsp;
	std::vector<OnLine> riders;
};

std::vector<std::unique_ptr<PolyCopy>> g_copies;   // [rc4l] by subsector, NULL for none
std::vector<int> g_subs;

// [rc4l] Points p, which points into from's array of n elements, at the same element of to's.
template <class T> T *Remap(T *p, const TArray<T> &from, TArray<T> &to)
{
	if (p == NULL || from.Size() == 0) return p;
	const ptrdiff_t i = p - &from[0];
	return i >= 0 && i < (ptrdiff_t)from.Size() ? &to[(unsigned)i] : p;
}

// [rc4l] The BSP copied with its pointers into its own arrays pointed at the copy's.
void CopyBSP(const FMiniBSP &src, FMiniBSP &dst)
{
	dst.bDirty = false;
	dst.Nodes = src.Nodes;
	dst.Segs = src.Segs;
	dst.Subsectors = src.Subsectors;
	dst.Verts = src.Verts;
	for (unsigned i = 0; i < dst.Segs.Size(); i++)
	{
		dst.Segs[i].v1 = Remap(src.Segs[i].v1, src.Verts, dst.Verts);
		dst.Segs[i].v2 = Remap(src.Segs[i].v2, src.Verts, dst.Verts);
	}
	for (unsigned i = 0; i < dst.Subsectors.Size(); i++)
		dst.Subsectors[i].firstline = Remap(src.Subsectors[i].firstline, src.Segs, dst.Segs);
	for (unsigned i = 0; i < dst.Nodes.Size(); i++)
		for (int c = 0; c < 2; c++)
		{
			void *child = src.Nodes[i].children[c];
			if ((size_t)child & 1)
			{
				subsector_t *leaf = (subsector_t *)((BYTE *)child - 1);
				dst.Nodes[i].children[c] = (BYTE *)Remap(leaf, src.Subsectors, dst.Subsectors) + 1;
			}
			else dst.Nodes[i].children[c] = Remap((node_t *)child, src.Nodes, dst.Nodes);
		}
	for (unsigned i = 0; i < dst.Verts.Size(); i++) { dst.Verts[i].rc.x = dst.Verts[i].x; dst.Verts[i].rc.y = dst.Verts[i].y; }
}

// [rc4l] Which copied vertices lie on a polyobject's line, found from the segs of its sides as the BSP was built.
void FindRiders(PolyCopy &c)
{
	c.riders.clear();
	std::vector<char> found(c.bsp.Verts.Size(), 0);
	for (unsigned i = 0; i < c.bsp.Segs.Size(); i++)
	{
		const seg_t &seg = c.bsp.Segs[i];
		if (seg.linedef == NULL || seg.sidedef == NULL || !(seg.sidedef->Flags & WALLF_POLYOBJ)) continue;
		const line_t *line = seg.linedef;
		const vertex_t *ends[2] = { seg.v1, seg.v2 };
		for (const vertex_t *v : ends)
		{
			const ptrdiff_t k = v - &c.bsp.Verts[0];
			if (k < 0 || k >= (ptrdiff_t)found.size() || found[k]) continue;
			found[k] = 1;
			const OnLine r = { int(k), line, ComputeAlongLine(line->v1->x.Raw(), line->v1->y.Raw(), line->v2->x.Raw(), line->v2->y.Raw(), v->x.Raw(), v->y.Raw()) };
			c.riders.push_back(r);
		}
	}
}

} // namespace

const FMiniBSP *PolyBSPOf(int subsector)
{
	return subsector >= 0 && subsector < (int)g_copies.size() && g_copies[subsector] ? &g_copies[subsector]->bsp : NULL;
}

const std::vector<int> &PolySubsectors()
{
	return g_subs;
}

void InitPolyBSPs()
{
	g_copies.clear();
	g_subs.clear();
}

void FillPolyBSPs()
{
	ZX_ZONE("copy/poly_fill");   // [rc4l] polyobjects linked, their BSPs built and copied
	if ((int)g_copies.size() != numsubsectors) g_copies.resize(numsubsectors);
	for (int s : g_subs) g_copies[s].reset();
	g_subs.clear();
	PO_LinkToSubsectors();
	for (int i = 0; i < po_NumPolyobjs; i++)
		for (FPolyNode *pn = polyobjs[i].subsectorlinks; pn != NULL; pn = pn->snext)
		{
			subsector_t *sub = pn->subsector;
			const int s = int(sub - subsectors);
			if (g_copies[s]) continue;
			// [rc4l] As AddPolyobjs builds it before walking it: missing or stale, and its segs owned by the subsector.
			if (sub->BSP == NULL || sub->BSP->bDirty)
			{
				sub->BuildPolyBSP();
				for (unsigned k = 0; k < sub->BSP->Segs.Size(); k++)
				{
					sub->BSP->Segs[k].Subsector = sub;
					sub->BSP->Segs[k].PartnerSeg = NULL;
				}
			}
			g_copies[s].reset(new PolyCopy());
			CopyBSP(*sub->BSP, g_copies[s]->bsp);
			FindRiders(*g_copies[s]);
			g_subs.push_back(s);
		}
	ZX_COUNT("copy/poly_subsectors", "subsectors", g_subs.size());
}

void BlendPolyBSPs()
{
	for (int s : g_subs)
	{
		PolyCopy &c = *g_copies[s];
		for (const OnLine &r : c.riders)
		{
			vertex_t &v = c.bsp.Verts[r.vert];
			long long x, y;
			ComputeOnLine(r.line->v1->rc.x.Raw(), r.line->v1->rc.y.Raw(), r.line->v2->rc.x.Raw(), r.line->v2->rc.y.Raw(), r.t, x, y);
			v.rc.x = fixed_t::FromRaw(x);
			v.rc.y = fixed_t::FromRaw(y);
			v.x = v.rc.x;
			v.y = v.rc.y;
		}
	}
}

}} // namespace zx::gamecopy
