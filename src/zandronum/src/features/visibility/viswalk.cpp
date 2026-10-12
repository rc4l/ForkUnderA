// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/visibility/viswalk.h"

#include "features/visibility/computation/clipranges_compute.h"
#include "features/visibility/computation/closedline_compute.h"
#include "features/levelmesh/wallcache.h"   // [rc4l] the backend's walk bits

#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "r_utility.h"
#include "textures/textures.h"
#include "c_cvars.h"
#include "c_dispatch.h"

// [rc4l] GL's fake flats and view area, which decide what a height transfer hides, until they are ported (P5).
#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"           // currentmapsection

#include <string.h>
#include <vector>

// [rc4l] Compare the walk against GL's own every frame GL walks, counting what differs for fua_vis_report.
CVAR(Bool, fua_vis_check, false, 0)

namespace zx { namespace visibility {

namespace {

ClipRanges g_clip;
std::vector<unsigned char> g_subs, g_sides, g_sectors;
// [rc4l] Each vertex's pseudo-angle this frame, stamped so a shared vertex is measured once.
std::vector<unsigned> g_vertAngle, g_vertStamp;
unsigned g_stamp = 1;
bool g_walked = false;
bool g_publish = false;
// [rc4l] The walk reached a sector with a stacked-sector plane, whose far side only that portal's own walk reaches.
bool g_sawPortal = false;
long long g_viewx = 0, g_viewy = 0;

int g_checks = 0, g_checkDiffers = 0, g_onlyGL = 0, g_onlyOurs = 0;
FString g_diffs;

bool HasTexture(side_t *side, int part)
{
	FTexture *tex = TexMan(side->GetTexture(part));
	return tex != NULL && tex->UseType != FTexture::TEX_Null;
}

unsigned ClipAngle(vertex_t *v)
{
	const ptrdiff_t i = v - vertexes;
	if (i < 0 || i >= numvertexes)
		return ComputePseudoAngle((double)(v->x - g_viewx).Raw(), (double)(v->y - g_viewy).Raw());
	if (g_vertStamp[i] != g_stamp)
	{
		g_vertStamp[i] = g_stamp;
		g_vertAngle[i] = ComputePseudoAngle((double)(v->x - g_viewx).Raw(), (double)(v->y - g_viewy).Raw());
	}
	return g_vertAngle[i];
}

// [rc4l] gl_CheckClip's question, with the heights read from the planes as GL reads them.
bool LineCloses(side_t *sidedef, sector_t *front, sector_t *back)
{
	ClosedLineIn in;
	in.stacked = back->portals[sector_t::ceiling] != NULL || back->portals[sector_t::floor] != NULL ||
		front->portals[sector_t::ceiling] != NULL || front->portals[sector_t::floor] != NULL;
	if (in.stacked) return false;
	line_t *linedef = sidedef->linedef;
	const secplane_t *planes[4] = { &front->floorplane, &front->ceilingplane, &back->floorplane, &back->ceilingplane };
	long long *heights[4] = { in.fsFloor, in.fsCeiling, in.bsFloor, in.bsCeiling };
	for (int p = 0; p < 4; p++)
	{
		const secplane_t &plane = *planes[p];
		if (plane.a | plane.b)
		{
			heights[p][0] = plane.ZatPoint(linedef->v1).Raw();
			heights[p][1] = plane.ZatPoint(linedef->v2).Raw();
		}
		else
		{
			// [rc4l] A flat floor's height is -d and a flat ceiling's d, as GL takes them.
			const long long h = (p == 0 || p == 2) ? (-plane.d).Raw() : plane.d.Raw();
			heights[p][0] = heights[p][1] = h;
		}
	}
	if (!ComputeLineMayClose(in)) return false;
	in.topTexture = HasTexture(sidedef, side_t::top);
	in.bottomTexture = HasTexture(sidedef, side_t::bottom);
	in.bothCeilingsSky = back->GetTexture(sector_t::ceiling) == skyflatnum && front->GetTexture(sector_t::ceiling) == skyflatnum;
	in.bothFloorsSky = back->GetTexture(sector_t::floor) == skyflatnum && front->GetTexture(sector_t::floor) == skyflatnum;
	return ComputeLineCloses(in);
}

subsector_t *g_sub;
sector_t *g_sector;

void AddLine(seg_t *seg)
{
	const unsigned startAngle = ClipAngle(seg->v2);
	const unsigned endAngle = ClipAngle(seg->v1);
	// [rc4l] Backface culling: GL draws a seg only from its front.
	if (startAngle - endAngle < 0x80000000u) return;
	if (seg->sidedef == NULL)
	{
		if (!(g_sub->flags & SSECF_DRAWN) && g_clip.SafeCheckRange(startAngle, endAngle)) g_sub->flags |= SSECF_DRAWN;
		return;
	}
	if (!g_clip.SafeCheckRange(startAngle, endAngle)) return;
	g_sub->flags |= SSECF_DRAWN;
	g_sides[seg->sidedef - sides] = 1;
	if (g_publish) zx::levelmesh::NoteMainVisible(numsubsectors + int(seg->sidedef - sides));

	if (!seg->backsector) g_clip.SafeAddClipRange(startAngle, endAngle);
	else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
	{
		if (g_sector->sectornum == seg->backsector->sectornum)
		{
			if (!HasTexture(seg->sidedef, side_t::mid)) return;
		}
		else
		{
			gl_CheckViewArea(seg->v1, seg->v2, seg->frontsector, seg->backsector);
			sector_t bs;
			sector_t *backsector = gl_FakeFlat(seg->backsector, &bs, true);
			if (LineCloses(seg->sidedef, g_sector, backsector)) g_clip.SafeAddClipRange(startAngle, endAngle);
		}
	}
	// [rc4l] The automap reveals what the player has seen.
	seg->linedef->flags |= ML_MAPPED;
}

bool BoxVisible(const fixed_t *bbox)
{
	const long long box[4] = { bbox[0].Raw(), bbox[1].Raw(), bbox[2].Raw(), bbox[3].Raw() };
	return ComputeBoxVisible(g_clip, g_viewx, g_viewy, box);
}

void PolySubsector(subsector_t *sub)
{
	seg_t *line = sub->firstline;
	for (int count = sub->numlines; count--; line++)
		if (line->linedef) AddLine(line);
}

void PolyNode(void *node)
{
	while (!((size_t)node & 1))
	{
		node_t *bsp = (node_t *)node;
		int side = R_PointOnSide(viewx, viewy, bsp);
		PolyNode(bsp->children[side]);
		side ^= 1;
		if (!BoxVisible(bsp->bbox[side])) return;
		node = bsp->children[side];
	}
	PolySubsector((subsector_t *)((BYTE *)node - 1));
}

void AddPolyobjs(subsector_t *sub)
{
	if (sub->BSP == NULL || sub->BSP->bDirty)
	{
		sub->BuildPolyBSP();
		for (unsigned i = 0; i < sub->BSP->Segs.Size(); i++)
		{
			sub->BSP->Segs[i].Subsector = sub;
			sub->BSP->Segs[i].PartnerSeg = NULL;
		}
	}
	if (sub->BSP->Nodes.Size() == 0) PolySubsector(&sub->BSP->Subsectors[0]);
	else PolyNode(&sub->BSP->Nodes.Last());
}

void DoSubsector(subsector_t *sub)
{
	sector_t *sector = sub->sector;
	if (!sector) return;
	if (!(currentmapsection[sub->mapsection >> 3] & (1 << (sub->mapsection & 7)))) return;
	g_subs[sub - subsectors] = 1;
	if (g_publish) zx::levelmesh::NoteMainVisible(int(sub - subsectors));
	if (!g_sectors[sector->sectornum])
	{
		g_sectors[sector->sectornum] = 1;
		sector->MoreFlags |= SECF_DRAWN;
		if (sector->portals[sector_t::ceiling] != NULL || sector->portals[sector_t::floor] != NULL) g_sawPortal = true;
	}
	sector_t fake;
	g_sector = gl_FakeFlat(sector, &fake, false);
	g_sub = sub;
	if (sub->polys != NULL)
	{
		AddPolyobjs(sub);
		return;
	}
	seg_t *seg = sub->firstline;
	for (int count = sub->numlines; count--; seg++)
	{
		if (seg->linedef == NULL)
		{
			if (!(sub->flags & SSECF_DRAWN)) AddLine(seg);
		}
		else if (!(seg->sidedef->Flags & WALLF_POLYOBJ)) AddLine(seg);
	}
}

void Node(void *node)
{
	while (!((size_t)node & 1))
	{
		node_t *bsp = (node_t *)node;
		int side = R_PointOnSide(viewx, viewy, bsp);
		Node(bsp->children[side]);
		side ^= 1;
		if (!BoxVisible(bsp->bbox[side])) return;
		node = bsp->children[side];
	}
	DoSubsector((subsector_t *)((BYTE *)node - 1));
}

} // namespace

void WalkView(bool publish)
{
	g_publish = publish;
	if (publish) zx::levelmesh::BeginMainVisibility();
	g_subs.assign(numsubsectors, 0);
	g_sides.assign(numsides, 0);
	g_sectors.assign(numsectors, 0);
	if ((int)g_vertStamp.size() != numvertexes) { g_vertStamp.assign(numvertexes, 0); g_vertAngle.assign(numvertexes, 0); }
	if (++g_stamp == 0) { g_vertStamp.assign(numvertexes, 0); g_stamp = 1; }
	g_viewx = viewx.Raw();
	g_viewy = viewy.Raw();
	g_clip.Clear();
	// [rc4l] GL's frustum, from its field of view and pitch, until the view's own lens moves here (P5).
	ComputeSeedFrustum(g_clip, viewangle, GLRenderer != NULL ? GLRenderer->FrustumAngle() : 0xffffffffu);
	g_sawPortal = false;
	if (numnodes == 0) DoSubsector(subsectors);
	else Node(nodes + numnodes - 1);
	g_walked = true;
	// [rc4l] Standalone draws no stacked views yet, so what lies through one is drawn in place and must not be culled.
	if (publish && g_sawPortal) zx::levelmesh::CancelMainVisibility();
}

bool SubsectorVisible(int i) { return g_walked && i >= 0 && i < (int)g_subs.size() && g_subs[i]; }
bool SideVisible(int i) { return g_walked && i >= 0 && i < (int)g_sides.size() && g_sides[i]; }
bool SectorVisible(int i) { return g_walked && i >= 0 && i < (int)g_sectors.size() && g_sectors[i]; }
bool WalkedThisFrame() { return g_walked; }

void ForgetWalk()
{
	g_walked = false;
	g_subs.clear();
	g_sides.clear();
	g_sectors.clear();
	g_vertStamp.clear();
	g_vertAngle.clear();
}

void CheckAgainstGL(const unsigned int *glBits, int words, int walkSlots, int mainSlot)
{
	if (!fua_vis_check || glBits == NULL) return;
	WalkView();
	g_checks++;
	int onlyGL = 0, onlyOurs = 0;
	const int ids = numsubsectors + numsides;
	for (int id = 0; id < ids; id++)
	{
		const long long bit = (long long)id * walkSlots + mainSlot;
		if (bit / 32 >= words) break;
		const bool gl = (glBits[bit / 32] >> (bit % 32)) & 1;
		const bool ours = id < numsubsectors ? g_subs[id] != 0 : g_sides[id - numsubsectors] != 0;
		if (gl && !ours) onlyGL++;
		if (ours && !gl) onlyOurs++;
		if (gl != ours && g_checkDiffers < 8 && g_diffs.Len() < 2000)
			g_diffs.AppendFormat("  %s %d: %s\n", id < numsubsectors ? "subsector" : "side",
				id < numsubsectors ? id : id - numsubsectors, gl ? "GL only" : "ours only");
	}
	if (onlyGL || onlyOurs) g_checkDiffers++;
	g_onlyGL += onlyGL;
	g_onlyOurs += onlyOurs;
}

}} // namespace zx::visibility

CCMD( fua_vis_report )
{
	Printf( "visibility: %d frames walked beside GL, %d differed (%d ids only GL reached, %d only ours)\n%s",
		zx::visibility::g_checks, zx::visibility::g_checkDiffers, zx::visibility::g_onlyGL, zx::visibility::g_onlyOurs,
		zx::visibility::g_diffs.GetChars( ) );
	zx::visibility::g_checks = zx::visibility::g_checkDiffers = zx::visibility::g_onlyGL = zx::visibility::g_onlyOurs = 0;
	zx::visibility::g_diffs = "";
}
