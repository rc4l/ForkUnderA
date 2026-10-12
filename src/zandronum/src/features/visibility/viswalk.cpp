// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/visibility/viswalk.h"

#include "features/visibility/computation/clipranges_compute.h"
#include "features/visibility/computation/closedline_compute.h"
#include "features/visibility/computation/coverage_compute.h"
#include "features/visibility/computation/mirrorview_compute.h"
#include "features/levelmesh/wallcache.h"   // [rc4l] the backend's walk bits and sky surfaces
#include "features/levelmesh/flatmesh.h"    // [rc4l] the stacked views, and the scene nesting a stacked view's sprites are captured in
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/fakeplanes.h"   // [rc4l] FakePlanesFor, a fill's model sector
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] the walk slots, the sky lists and a sky hole's size
#include "features/surfaces/computation/skywall_compute.h"
#include "features/surfaces/computation/wallgeom_compute.h"   // [rc4l] where a pinched wall ends, as SetWallCoordinates moves it
#include "features/materials/materials.h"     // [rc4l] whether a one-sided wall has a texture, as GL asks
#include "features/hwrender/spriteview.h"   // [rc4l] the view's actor, which a skybox may not be
#include "features/fixed64/computation/angle_interp_compute.h"   // [rc4l] a skybox's turn, as GL interpolates it
#include "g_shared/a_sharedglobal.h"        // [rc4l] ASkyViewpoint
#include "p_lnspec.h"                       // [rc4l] Line_Horizon
#include "p_3dfloors.h"                     // [rc4l] F3DFloor, a 3D floor's control sector
#include "c_console.h"                      // [rc4l] C_ShouldForceInterpolation
#include "p_local.h"                        // [rc4l] P_PointOnLineSide, a mirror's clip

#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "r_utility.h"
#include "textures/textures.h"
#include "c_cvars.h"
#include "c_dispatch.h"

// [rc4l] GL's fake flats, view area and portal coverage, which decide what a height transfer hides and where a stacked view starts, until they are ported (P5).
#include "gl/renderer/gl_renderer.h"
#include "gl/data/gl_data.h"           // currentmapsection, FPortal, gl_BuildPortalCoverage
#include "gl/renderer/gl_lightdata.h"  // gl_ClampLight, gl_isBlack, gl_fixedcolormap: a mirror sheen's light

#include <string.h>
#include <vector>

// [rc4l] Compare the walk against GL's own every frame GL walks, counting what differs for fua_vis_report.
CVAR(Bool, fua_vis_check, false, 0)
EXTERN_CVAR(Bool, gl_noskyboxes)
EXTERN_CVAR(Bool, gl_mirrors)
EXTERN_CVAR(Bool, gl_mirror_envmap)

namespace zx { namespace visibility {

namespace {

// [rc4l] A portal a walk looked into, in the order GL makes its GLPortal: a stacked view, a skybox or a line mirror, the lines GL's portal keeps (x1, y1, x2, y2, raw), a stacked view's slot, and the subsectors its coverage starts from.
struct PortalView
{
	FPortal *stack;
	ASkyViewpoint *skybox;
	line_t *mirror;
	int slot;
	std::vector<long long> lines;
	std::vector<subsector_t *> subs;
};

// [rc4l] What the walk in progress is for.
struct WalkSpec
{
	int slot;                         // [rc4l] the walk-bit slot it marks, or -1
	int skyList;                      // [rc4l] the kSkyCapture* list its sky surfaces fill
	bool player;                      // [rc4l] the player's own walk: stacked walls, and the flats pass that gives stacked views their subsectors
	AActor *viewActor;                // [rc4l] GL's mViewActor, whose own skybox is not entered
	std::vector<PortalView> *portals; // [rc4l] where the skyboxes (and for the player the stacked views) it looks into go, or NULL
	const unsigned char *covered;     // [rc4l] a stacked view's covered subsectors, unclipped on reaching them, or NULL
	const unsigned char *nodeCovered; // [rc4l] and the nodes over them, entered whatever the clipper says
	line_t *mirror;                   // [rc4l] a line mirror's walk, which leaves out what lies behind the mirror, or NULL
	void (*particles)(subsector_t *sub, sector_t *sector);   // [rc4l] where each subsector reached goes for its particles, or NULL
};

WalkSpec g_spec;
// [rc4l] The walk makes sky surfaces, stacked walls or portals, so it builds each linedef's walls once.
bool g_deriving = false;
ClipRanges g_clip;
std::vector<unsigned char> g_subs, g_sides, g_sectors;
// [rc4l] Sectors in the order the walk first reached them, as GL's RenderThings takes them.
std::vector<sector_t *> g_sectorOrder;
// [rc4l] Each vertex's pseudo-angle this walk, stamped so a shared vertex is measured once.
std::vector<unsigned> g_vertAngle, g_vertStamp;
unsigned g_stamp = 1;
long long g_viewx = 0, g_viewy = 0;
// [rc4l] Every walk's subsectors, sidedefs and sectors since the player's last derived walk, stamped with that walk's frame, and the sidedefs and sectors as lists.
std::vector<unsigned> g_subSeen, g_sideSeen, g_sectorSeen;
std::vector<int> g_seenSides, g_seenSectors;
unsigned g_seenFrame = 1;
// [rc4l] Linedefs whose walls this walk has built, since GL builds a line's once a walk from its first visible seg.
std::vector<unsigned char> g_lineDone;
const int kNoSkyDraw = 89;   // [rc4l] gl_sky.cpp's NoSkyDraw sector special

// [rc4l] The player's walk's flats pass: subsectors GL processed (SSRF_PROCESSED), render sectors processed, and the stacked planes facing the eye.
std::vector<unsigned char> g_subProcessed, g_secProcessed;
std::vector<sector_t *> g_planeStacks[2];
// [rc4l] The portals the player's last derived walk looked into, and its eye.
std::vector<PortalView> g_playerPortals;
long long g_playerX = 0, g_playerY = 0;
// [rc4l] The BSP as coverage nodes, built once a level, and a stacked view's covered subsectors and nodes.
std::vector<CoverageNode> g_coverageNodes;
std::vector<unsigned char> g_covered, g_nodeCovered;
// [rc4l] ProcessSectorStacks' validcount, per subsector.
std::vector<unsigned> g_subStamp;
unsigned g_subStampNow = 0;

int g_checks = 0, g_checkDiffers = 0, g_onlyGL = 0, g_onlyOurs = 0;
FString g_diffs;
int g_skyChecks = 0, g_skyDiffers = 0;
FString g_skyDiffs;
int g_portalChecks = 0, g_portalDiffers = 0, g_portalUnwalked = 0;
// [rc4l] What the portal-view check had to compare, summed over its frames.
int g_stackViewsSeen = 0, g_mirrorsSeen = 0, g_sheensSeen = 0;
FString g_portalDiffs;
// [rc4l] The stacked views and skybox walks derived beside GL's walk, held until GL has walked its own portals.
zx::levelmesh::PortalViewState g_oursPortals;
bool g_oursReady = false;

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

// [rc4l] A sector whose change re-bakes something a walk reached, and the control sectors of the 3D floors in it, which re-bake it with them.
void SeeSector(const sector_t *s)
{
	if (s == NULL) return;
	const int i = s->sectornum;
	if (g_sectorSeen[i] == g_seenFrame) return;
	g_sectorSeen[i] = g_seenFrame;
	g_seenSectors.push_back(i);
	const sector_t &real = sectors[i];
	if (real.e == NULL) return;
	for (unsigned k = 0; k < real.e->XFloor.ffloors.Size(); k++) SeeSector(real.e->XFloor.ffloors[k]->model);
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

// [rc4l] The walk's portal for a stacked plane or a skybox, made the first time it is met, as GetGLPortal and FindPortal make GL's.
PortalView &PortalFor(FPortal *stack, ASkyViewpoint *skybox, line_t *mirror = NULL)
{
	std::vector<PortalView> &list = *g_spec.portals;
	for (size_t i = 0; i < list.size(); i++)
		if (list[i].stack == stack && list[i].skybox == skybox && list[i].mirror == mirror) return list[i];
	PortalView p;
	p.stack = stack;
	p.skybox = skybox;
	p.mirror = mirror;
	p.slot = -1;
	list.push_back(p);
	return list.back();
}

// [rc4l] A wall GL adds to its portal's lines, kept as GLPortal::ClearClipper reads them: the glseg back in fixed point.
void AddPortalLine(PortalView &p, float x1, float y1, float x2, float y2)
{
	p.lines.push_back((long long)FLOAT2FIXED(x1));
	p.lines.push_back((long long)FLOAT2FIXED(y1));
	p.lines.push_back((long long)FLOAT2FIXED(x2));
	p.lines.push_back((long long)FLOAT2FIXED(y2));
}

// [rc4l] GLWall::SkyPlane: a stacked plane's wall into its view, a sky's into the walk's sky list with the skybox it names, and either's line into its portal.
void SkyPlane(sector_t *sector, int plane, float x1, float y1, float x2, float y2, const zx::surfaces::SkyWallQuad &q, int special)
{
	FPortal *portal = sector->portals[plane];
	if (portal != NULL)
	{
		// [rc4l] Only the player's walk's stacked walls are captured, as RegisterStackWall takes only its.
		if (!g_spec.player) return;
		PortalView &p = PortalFor(portal, NULL);
		const int slot = zx::levelmesh::AddStackWall(portal, x1, y1, x2, y2, q.zbottom, q.ztop);
		if (slot >= 0) p.slot = slot;
		AddPortalLine(p, x1, y1, x2, y2);
		return;
	}
	if (sector->GetTexture(plane) != skyflatnum) return;
	ASkyViewpoint *skybox = sector->GetSkyBox(plane);
	if (gl_noskyboxes || skybox == NULL || g_spec.viewActor == skybox || (skybox->flags & MF_JUSTHIT)) skybox = NULL;
	if (skybox != NULL && g_spec.portals != NULL) AddPortalLine(PortalFor(NULL, skybox), x1, y1, x2, y2);
	zx::levelmesh::AddViewSkyQuad(g_spec.skyList, x1, y1, x2, y2, q.zbottom, q.ztop, skybox, special);
}

// [rc4l] A one-sided Line_Mirror wall the player's walk reached, as GLWall::DoTexture and PutWall take it: its portal, and its sheen in the light of the first piece SplitWall puts.
void NoteMirror(seg_t *seg, sector_t *fs, float x1, float y1, float x2, float y2, const float *zfloor, const float *zceil)
{
	// [rc4l] GLWall::Process's light for the wall, before any light band.
	FColormap cm;
	cm = fs->ColorMap;
	const int baseLight = gl_ClampLight(fs->lightlevel);
	const bool foggy = !gl_isBlack(cm.FadeColor) || (level.flags & LEVEL_HASFADETABLE);
	int rel = 0;
	int light = gl_ClampLight(seg->sidedef->GetLightLevel(foggy, baseLight, false, &rel));
	TArray<lightlist_t> &bands = g_sub->sector->e->XFloor.lightlist;
	int band = zx::visibility::kSheenOwnLight;
	if (bands.Size() > 0 && !gl_fixedcolormap)
	{
		zx::surfaces::WallPinch pinch;
		zx::surfaces::ComputeWallPinch(zceil, zfloor, pinch);
		SheenWall w = { x1 + pinch.fracLeft * (x2 - x1), y1 + pinch.fracLeft * (y2 - y1), x1 + pinch.fracRight * (x2 - x1),
			y1 + pinch.fracRight * (y2 - y1), { pinch.ztop[0], pinch.ztop[1] }, { pinch.zbottom[0], pinch.zbottom[1] } };
		std::vector<SheenPlane> planes;
		for (unsigned i = 1; i < bands.Size(); i++)
		{
			const secplane_t &p = bands[i].plane;
			const SheenPlane sp = { double(p.a), double(p.b), double(p.d), double(p.ic), (p.a | p.b) != 0 };
			planes.push_back(sp);
		}
		band = ComputeSheenBand(w, planes.empty() ? NULL : &planes[0], int(bands.Size()));
		if (band == kSheenNothing) return;   // [rc4l] SplitWall put no piece, so GL has no mirror here
	}
	PortalView &p = PortalFor(NULL, NULL, seg->linedef);
	AddPortalLine(p, x1, y1, x2, y2);
	if (!gl_mirror_envmap) return;
	if (gl_fixedcolormap) cm.Clear();
	if (band >= 0)
	{
		// [rc4l] GLWall::Put3DWall's light for the band.
		const lightlist_t &l = bands[band];
		const bool fadewall = l.caster && (l.caster->flags & FF_FADEWALLS) && !gl_isBlack(l.extra_colormap->Fade) && gl_isBlack(cm.FadeColor);
		if (l.p_lightlevel != &seg->sidedef->sector->lightlevel) light = gl_ClampLight(*l.p_lightlevel);
		cm.CopyLightColor(l.extra_colormap);
		if (fadewall) light = 255;
	}
	zx::levelmesh::AddMirrorSheen(int(seg->linedef - lines), light, cm);
}

// [rc4l] The sky and stacked walls GLWall::Process raises from this seg, from front sector `fs` and back `bs` as GL's walk hands them over, and a mirror's portal.
void DeriveSkyWalls(seg_t *seg, sector_t *fs, sector_t *bs)
{
	vertex_t *v1, *v2;
	if (seg->sidedef->Flags & WALLF_POLYOBJ) { v1 = seg->v1; v2 = seg->v2; }
	else if (seg->sidedef == seg->linedef->sidedef[0]) { v1 = seg->linedef->v1; v2 = seg->linedef->v2; }
	else { v1 = seg->linedef->v2; v2 = seg->linedef->v1; }
	const float x1 = FIXED2FLOAT(v1->x), y1 = FIXED2FLOAT(v1->y), x2 = FIXED2FLOAT(v2->x), y2 = FIXED2FLOAT(v2->y);
	float zfloor[2], zceil[2];
	if (fs->floorplane.a | fs->floorplane.b)
	{
		zfloor[0] = FIXED2FLOAT(fs->floorplane.ZatPoint(v1));
		zfloor[1] = FIXED2FLOAT(fs->floorplane.ZatPoint(v2));
	}
	else zfloor[0] = zfloor[1] = FIXED2FLOAT(-fs->floorplane.d);
	if (fs->ceilingplane.a | fs->ceilingplane.b)
	{
		zceil[0] = FIXED2FLOAT(fs->ceilingplane.ZatPoint(v1));
		zceil[1] = FIXED2FLOAT(fs->ceilingplane.ZatPoint(v2));
	}
	else zceil[0] = zceil[1] = FIXED2FLOAT(fs->ceilingplane.d);
	const int special = seg->linedef->special;

	if (special == Line_Horizon || !bs || !(seg->linedef->flags & (ML_TWOSIDED | ML_3DMIDTEX)))
	{
		zx::surfaces::SkyWallQuad q[2];
		zx::surfaces::ComputeSkyNormal(zfloor, zceil, q);
		SkyPlane(fs, sector_t::ceiling, x1, y1, x2, y2, q[0], special);
		SkyPlane(fs, sector_t::floor, x1, y1, x2, y2, q[1], special);
		if (special == Line_Horizon)
		{
			const int n = zx::surfaces::ComputeHorizonSky(viewz.Raw(), fs->GetPlaneTexZ(sector_t::ceiling).Raw(), fs->GetPlaneTexZ(sector_t::floor).Raw(),
				fs->GetTexture(sector_t::ceiling) == skyflatnum, fs->GetTexture(sector_t::floor) == skyflatnum, q);
			for (int i = 0; i < n; i++) SkyPlane(fs, q[i].ceiling ? sector_t::ceiling : sector_t::floor, x1, y1, x2, y2, q[i], special);
		}
		// [rc4l] An untextured one-sided wall is a hole GL's sky fills.
		else if (!zx::materials::MaterialForTexture(TexMan(seg->sidedef->GetTexture(side_t::mid))))
			zx::levelmesh::AddViewSkyHole(g_spec.skyList, x1, y1, x2, y2, zfloor, zceil);
		else if (g_spec.player && special == Line_Mirror && gl_mirrors) NoteMirror(seg, fs, x1, y1, x2, y2, zfloor, zceil);
		return;
	}
	zx::surfaces::SkyTopIn t;
	t.frontSky = fs->GetTexture(sector_t::ceiling) == skyflatnum;
	t.backSky = bs->GetTexture(sector_t::ceiling) == skyflatnum;
	t.backNoSkyDraw = (bs->special & 0xff) == kNoSkyDraw;
	t.backOpen = bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) || bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2) || bs->transdoor;
	t.icarus = bs->floorplane.a == 0 && bs->floorplane.b == 0 && fs->floorplane.a == 0 && fs->floorplane.b == 0 &&
		bs->GetPlaneTexZ(sector_t::floor) == fs->GetPlaneTexZ(sector_t::floor) + FRACUNIT;
	t.hasBottom = HasTexture(seg->sidedef, side_t::bottom);
	t.midExists = TexMan(seg->sidedef->GetTexture(side_t::mid)) != NULL;
	t.dontPegTop = (seg->linedef->flags & ML_DONTPEGTOP) != 0;
	t.midOffsetDown = seg->sidedef->GetTextureYOffset(side_t::mid) > 0;
	t.backCeilingPlusMid = FIXED2FLOAT(bs->ceilingplane.ZatPoint(v2) + seg->sidedef->GetTextureYOffset(side_t::mid));
	t.zceil[0] = zceil[0]; t.zceil[1] = zceil[1];
	t.backCeiling[0] = FIXED2FLOAT(bs->ceilingplane.ZatPoint(v1));
	t.backCeiling[1] = FIXED2FLOAT(bs->ceilingplane.ZatPoint(v2));
	t.frontReflect = fs->GetReflect(sector_t::ceiling) > 0;
	t.backReflectSameHeight = bs->GetReflect(sector_t::ceiling) > 0 && bs->ceilingplane.d == fs->ceilingplane.d;
	t.frontPortal = fs->portals[sector_t::ceiling] != NULL;
	t.samePortal = fs->portals[sector_t::ceiling] == bs->portals[sector_t::ceiling];
	zx::surfaces::SkyWallQuad q;
	if (zx::surfaces::ComputeSkyTop(t, q)) SkyPlane(fs, sector_t::ceiling, x1, y1, x2, y2, q, special);

	zx::surfaces::SkyBottomIn b;
	b.frontSky = fs->GetTexture(sector_t::floor) == skyflatnum;
	b.backSky = bs->GetTexture(sector_t::floor) == skyflatnum;
	b.backNoSkyDraw = t.backNoSkyDraw;
	b.bottomNull = !HasTexture(seg->sidedef, side_t::bottom);
	b.backOpen = bs->ceilingplane.ZatPoint(v1) > bs->floorplane.ZatPoint(v1) || bs->ceilingplane.ZatPoint(v2) > bs->floorplane.ZatPoint(v2);
	b.eyeBelowBackFloor = bs->floorplane.ZatPoint(FIXED2FLOAT(viewx), FIXED2FLOAT(viewy)) > FIXED2FLOAT(viewz);
	b.zfloor[0] = zfloor[0]; b.zfloor[1] = zfloor[1];
	b.backFloor[0] = FIXED2FLOAT(bs->floorplane.ZatPoint(v1));
	b.backFloor[1] = FIXED2FLOAT(bs->floorplane.ZatPoint(v2));
	b.frontReflect = fs->GetReflect(sector_t::floor) > 0;
	b.backReflectSameHeight = bs->GetReflect(sector_t::floor) > 0 && bs->floorplane.d == fs->floorplane.d;
	b.frontPortal = fs->portals[sector_t::floor] != NULL;
	b.samePortal = fs->portals[sector_t::floor] == bs->portals[sector_t::floor];
	if (zx::surfaces::ComputeSkyBottom(b, q)) SkyPlane(fs, sector_t::floor, x1, y1, x2, y2, q, special);
}

// [rc4l] GLMirrorPortal::ClipSeg and ClipSubsector: in a mirror's walk, whatever lies wholly behind the mirror.
bool BehindMirror(const vertex_t *v) { return P_PointOnLineSide(v->x, v->y, g_spec.mirror) != 0; }

bool SubsectorBehindMirror(subsector_t *sub)
{
	for (DWORD i = 0; i < sub->numlines; i++)
		if (!BehindMirror(sub->firstline[i].v1)) return false;
	return true;
}

void AddLine(seg_t *seg)
{
	if (g_spec.mirror != NULL && BehindMirror(seg->v1) && BehindMirror(seg->v2)) return;
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
	const int side = int(seg->sidedef - sides);
	g_sides[side] = 1;
	if (g_sideSeen[side] != g_seenFrame)
	{
		g_sideSeen[side] = g_seenFrame;
		g_seenSides.push_back(side);
		SeeSector(seg->frontsector);
		SeeSector(seg->backsector);
	}
	if (g_spec.slot >= 0) zx::levelmesh::NoteSlotVisible(g_spec.slot, numsubsectors + side);

	sector_t bs;
	sector_t *backsector = NULL;
	if (!seg->backsector) g_clip.SafeAddClipRange(startAngle, endAngle);
	else if (!(seg->sidedef->Flags & WALLF_POLYOBJ))
	{
		if (g_sector->sectornum == seg->backsector->sectornum)
		{
			if (!HasTexture(seg->sidedef, side_t::mid)) return;
			backsector = g_sector;
		}
		else
		{
			gl_CheckViewArea(seg->v1, seg->v2, seg->frontsector, seg->backsector);
			backsector = gl_FakeFlat(seg->backsector, &bs, true);
			if (LineCloses(seg->sidedef, g_sector, backsector)) g_clip.SafeAddClipRange(startAngle, endAngle);
		}
	}
	else backsector = g_sector;
	// [rc4l] The automap reveals what the player has seen.
	seg->linedef->flags |= ML_MAPPED;
	if (g_deriving)
	{
		const bool polyobj = (seg->sidedef->Flags & WALLF_POLYOBJ) != 0;
		const int line = int(seg->linedef - lines);
		if (polyobj || !g_lineDone[line])
		{
			if (!polyobj) g_lineDone[line] = 1;
			DeriveSkyWalls(seg, g_sector, backsector);
		}
	}
}

// [rc4l] UnclipSubsector: a subsector a stacked view's portal covers was seen from the other side, so the clipper opens over its segs.
void UnclipSubsector(subsector_t *sub)
{
	seg_t *seg = sub->firstline;
	for (int count = sub->numlines; count--; seg++)
	{
		const unsigned startAngle = ClipAngle(seg->v2);
		const unsigned endAngle = ClipAngle(seg->v1);
		if (startAngle - endAngle >= 0x80000000u) g_clip.SafeRemoveClipRange(startAngle, endAngle);
	}
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

// [rc4l] GL's flats pass for a subsector (DoSubsector, GLFlat::ProcessSector), as far as stacked views need it: the subsectors that open into a stacked plane, and the stacked planes that face the eye.
void FlatsPass(subsector_t *sub, sector_t *sector)
{
	if (sub->numlines <= 2 && !(sub->hacked & 1)) return;
	if (sector->heightsec && !(sector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC) && in_area == area_default) return;
	sector_t fake;
	sector_t *flat = g_sector;
	if (sector != sub->render_sector) flat = gl_FakeFlat(sub->render_sector, &fake, false);
	const int rs = sub->render_sector->sectornum;
	if (!g_secProcessed[rs])
	{
		g_secProcessed[rs] = 1;
		const float vx = FIXED2FLOAT(viewx), vy = FIXED2FLOAT(viewy), vz = FIXED2FLOAT(viewz);
		sector_t *real = &sectors[flat->sectornum];
		if (flat->portals[sector_t::floor] != NULL && flat->floorplane.ZatPoint(vx, vy) <= vz) g_planeStacks[sector_t::floor].push_back(real);
		if (flat->portals[sector_t::ceiling] != NULL && flat->ceilingplane.ZatPoint(vx, vy) >= vz) g_planeStacks[sector_t::ceiling].push_back(real);
	}
	g_subProcessed[sub - subsectors] = 1;
	if (flat->portals[sector_t::ceiling] != NULL) PortalFor(flat->portals[sector_t::ceiling], NULL).subs.push_back(sub);
	if (flat->portals[sector_t::floor] != NULL) PortalFor(flat->portals[sector_t::floor], NULL).subs.push_back(sub);
}

void DoSubsector(subsector_t *sub)
{
	sector_t *sector = sub->sector;
	if (!sector) return;
	if (!(currentmapsection[sub->mapsection >> 3] & (1 << (sub->mapsection & 7)))) return;
	const int si = int(sub - subsectors);
	if (g_spec.covered != NULL && g_spec.covered[si]) UnclipSubsector(sub);
	if (g_spec.mirror != NULL && SubsectorBehindMirror(sub)) return;
	g_subs[si] = 1;
	if (g_subSeen[si] != g_seenFrame)
	{
		g_subSeen[si] = g_seenFrame;
		SeeSector(sector);
		SeeSector(sub->render_sector);
		int fills = 0;
		const zx::levelmesh::FakePlane *fill = zx::levelmesh::FakePlanesFor(si, fills);
		for (int k = 0; k < fills; k++) SeeSector(&sectors[fill[k].model]);
	}
	if (g_spec.slot >= 0) zx::levelmesh::NoteSlotVisible(g_spec.slot, si);
	if (!g_sectors[sector->sectornum])
	{
		g_sectors[sector->sectornum] = 1;
		g_sectorOrder.push_back(sector);
		sector->MoreFlags |= SECF_DRAWN;
	}
	sector_t fake;
	g_sector = gl_FakeFlat(sector, &fake, false);
	g_sub = sub;
	if (g_spec.particles != NULL) g_spec.particles(sub, g_sector);
	if (sub->polys != NULL) AddPolyobjs(sub);
	else
	{
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
	if (g_spec.player) FlatsPass(sub, sector);
}

void Node(void *node)
{
	while (!((size_t)node & 1))
	{
		node_t *bsp = (node_t *)node;
		int side = R_PointOnSide(viewx, viewy, bsp);
		Node(bsp->children[side]);
		side ^= 1;
		if (!BoxVisible(bsp->bbox[side]) && !(g_spec.nodeCovered != NULL && g_spec.nodeCovered[bsp - nodes])) return;
		node = bsp->children[side];
	}
	DoSubsector((subsector_t *)((BYTE *)node - 1));
}

// [rc4l] One walk from the current eye, its clipper already seeded.
void Walk(const WalkSpec &spec)
{
	g_spec = spec;
	g_deriving = spec.player || spec.skyList != zx::levelmesh::kSkyCaptureNone || spec.portals != NULL;
	g_subs.assign(numsubsectors, 0);
	g_sides.assign(numsides, 0);
	g_sectors.assign(numsectors, 0);
	g_sectorOrder.clear();
	if (g_deriving) g_lineDone.assign(numlines, 0);
	if ((int)g_vertStamp.size() != numvertexes) { g_vertStamp.assign(numvertexes, 0); g_vertAngle.assign(numvertexes, 0); }
	if (++g_stamp == 0) { g_vertStamp.assign(numvertexes, 0); g_stamp = 1; }
	g_viewx = viewx.Raw();
	g_viewy = viewy.Raw();
	if ((int)g_subSeen.size() != numsubsectors || (int)g_sideSeen.size() != numsides || (int)g_sectorSeen.size() != numsectors)
	{
		g_subSeen.assign(numsubsectors, 0);
		g_sideSeen.assign(numsides, 0);
		g_sectorSeen.assign(numsectors, 0);
	}
	if (numnodes == 0) DoSubsector(subsectors);
	else Node(nodes + numnodes - 1);
}

// [rc4l] Whether a subsector's plane looks the same as the stacked plane's, as CollectSectorStacksCeiling and Floor ask.
bool SameVisplane(sector_t *me, sector_t *anchor, int plane)
{
	const bool ceiling = plane == sector_t::ceiling;
	return me->GetTexture(plane) == anchor->GetTexture(plane) &&
		!(ceiling ? me->ceilingplane != anchor->ceilingplane : me->floorplane != anchor->floorplane) &&
		(ceiling ? me->GetCeilingLight() == anchor->GetCeilingLight() : me->GetFloorLight() == anchor->GetFloorLight()) &&
		me->ColorMap == anchor->ColorMap &&
		me->GetXOffset(plane) == anchor->GetXOffset(plane) && me->GetYOffset(plane) == anchor->GetYOffset(plane) &&
		me->GetXScale(plane) == anchor->GetXScale(plane) && me->GetYScale(plane) == anchor->GetYScale(plane) &&
		me->GetAngle(plane) == anchor->GetAngle(plane);
}

void CollectStackSubs(subsector_t *sub, sector_t *anchor, int plane, std::vector<subsector_t *> &handled)
{
	g_subStamp[sub - subsectors] = g_subStampNow;
	if (sub->render_sector->portals[plane] != NULL) return;
	if (sub->numlines > 2 && !g_subProcessed[sub - subsectors]) return;
	sector_t fake;
	if (!SameVisplane(gl_FakeFlat(sub->render_sector, &fake, false), anchor, plane)) return;
	handled.push_back(sub);
	for (DWORD j = 0; j < sub->numlines; j++)
	{
		seg_t *seg = sub->firstline + j;
		if (seg->PartnerSeg != NULL && g_subStamp[seg->PartnerSeg->Subsector - subsectors] != g_subStampNow)
			CollectStackSubs(seg->PartnerSeg->Subsector, anchor, plane, handled);
	}
}

// [rc4l] FDrawInfo::ProcessSectorStacks: the subsectors beside a stacked plane that look the same as it join its view's coverage, ceilings first.
void ProcessSectorStacks()
{
	if ((int)g_subStamp.size() != numsubsectors) { g_subStamp.assign(numsubsectors, 0); g_subStampNow = 0; }
	static const int order[2] = { sector_t::ceiling, sector_t::floor };
	std::vector<subsector_t *> handled;
	for (int o = 0; o < 2; o++)
	{
		const int plane = order[o];
		if (++g_subStampNow == 0) { g_subStamp.assign(numsubsectors, 0); g_subStampNow = 1; }
		for (size_t i = 0; i < g_planeStacks[plane].size(); i++)
		{
			sector_t fake;
			sector_t *sec = gl_FakeFlat(g_planeStacks[plane][i], &fake, false);
			FPortal *portal = sec->portals[plane];
			if (portal == NULL) continue;
			for (int k = 0; k < sec->subsectorcount; k++)
			{
				subsector_t *sub = sec->subsectors[k];
				if (!g_subProcessed[sub - subsectors]) continue;
				handled.clear();
				for (DWORD j = 0; j < sub->numlines; j++)
				{
					seg_t *seg = sub->firstline + j;
					if (seg->PartnerSeg != NULL && g_subStamp[seg->PartnerSeg->Subsector - subsectors] != g_subStampNow)
						CollectStackSubs(seg->PartnerSeg->Subsector, sec, plane, handled);
				}
				for (size_t h = 0; h < handled.size(); h++)
				{
					if (handled[h]->portalcoverage[plane].subsectors == NULL)
						gl_BuildPortalCoverage(&handled[h]->portalcoverage[plane], handled[h], portal);
					PortalFor(portal, NULL).subs.push_back(handled[h]);
				}
			}
		}
	}
}

void EnsureCoverageNodes()
{
	if ((int)g_coverageNodes.size() == numnodes) return;
	g_coverageNodes.resize(numnodes);
	for (int i = 0; i < numnodes; i++)
		for (int c = 0; c < 2; c++)
		{
			void *child = nodes[i].children[c];
			g_coverageNodes[i].child[c] = ((size_t)child & 1)
				? ~int((subsector_t *)((BYTE *)child - 1) - subsectors) : int((node_t *)child - nodes);
		}
}

// [rc4l] The sprite view inside a derived portal view, standing in for GL's mCurrentPortal and mViewActor there: no actor of its own, and in a mirror the mirror's clip; the rest is the outer view's.
const zx::hwrender::SpriteViewProvider *g_outerView = NULL;
line_t *g_viewMirror = NULL;
AActor *PortalViewActor() { return NULL; }
void PortalViewVector(float &x, float &y) { g_outerView->ViewVector(x, y); }
int PortalSpriteIndex() { return g_outerView->NextSpriteIndex(); }
bool PortalRejects(fixed_t x, fixed_t y) { return g_viewMirror != NULL && P_PointOnLineSide(x, y, g_viewMirror) != 0; }
bool PortalMirrors() { return g_viewMirror != NULL; }
int PortalLightCount() { return g_outerView->DynamicLightCount(); }
void PortalViewAngles(float &yaw, float &pitch) { g_outerView->ViewAngles(yaw, pitch); }
FTexture *PortalParticle(int style) { return g_outerView->ParticleTexture(style); }
const zx::hwrender::SpriteViewProvider g_portalView =
{
	PortalViewActor, PortalViewVector, PortalSpriteIndex, PortalRejects, PortalMirrors, PortalLightCount, PortalViewAngles, PortalParticle
};

// [rc4l] The actors in the sectors the last walk reached, handed to `sprite` inside a portal view, as GL's walk hands them to ProcessSprite.
void PortalSprites(void (*sprite)(AActor *thing), line_t *mirror)
{
	const std::vector<sector_t *> reached(g_sectorOrder);
	g_outerView = &zx::hwrender::SpriteView();
	g_viewMirror = mirror;
	zx::hwrender::SetSpriteViewProvider(&g_portalView);
	for (size_t i = 0; i < reached.size(); i++)
		for (AActor *thing = reached[i]->thinglist; thing != NULL; thing = thing->snext) sprite(thing);
	zx::hwrender::SetSpriteViewProvider(g_outerView);
	g_viewMirror = NULL;
}

// [rc4l] GLSkyboxPortal::DrawContents's walk: from the viewpoint where this frame finds it, turned as it has turned, the clipper opened only where its sky walls were seen from (seenX, seenY), into the skybox walk slot.
void WalkSkybox(const PortalView &p, long long seenX, long long seenY)
{
	ASkyViewpoint *origin = p.skybox;
	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	sector_t *const ssector = viewsector;
	const area_t sarea = in_area;
	AActor *const sactor = GLRenderer->mViewActor;
	const fixed_t frac = C_ShouldForceInterpolation() ? FRACUNIT : r_TicFrac;
	viewx = origin->PrevX + FixedMul(frac, origin->x - origin->PrevX);
	viewy = origin->PrevY + FixedMul(frac, origin->y - origin->PrevY);
	viewz = origin->PrevZ + FixedMul(frac, origin->z - origin->PrevZ);
	viewangle += zx::InterpolateAngleBAM(origin->PrevAngle, origin->angle, (int64_t)frac);
	// [rc4l] Not too close to the viewpoint's floor or ceiling, as GL keeps it.
	const fixed_t floorh = origin->Sector->floorplane.ZatPoint(origin->x, origin->y);
	const fixed_t ceilh = origin->Sector->ceilingplane.ZatPoint(origin->x, origin->y);
	if (viewz < floorh + 4 * FRACUNIT) viewz = floorh + 4 * FRACUNIT;
	if (viewz > ceilh - 4 * FRACUNIT) viewz = ceilh - 4 * FRACUNIT;
	GLRenderer->mViewActor = origin;
	origin->flags |= MF_JUSTHIT;
	GLRenderer->SetViewArea();
	ComputeSeedPortal(g_clip, seenX, seenY, &p.lines[0], int(p.lines.size() / 4), viewangle - sa, viewangle, GLRenderer->FrustumAngle());
	const int mapsection = R_PointInSubsector(viewx, viewy)->mapsection;
	TArray<BYTE> sections(currentmapsection);
	if (currentmapsection.Size() > 0) memset(&currentmapsection[0], 0, currentmapsection.Size());
	currentmapsection[mapsection >> 3] |= 1 << (mapsection & 7);
	const WalkSpec spec = { zx::levelmesh::kSkyboxWalkSlot, zx::levelmesh::kSkyCaptureNone, false, origin, NULL, NULL, NULL };
	Walk(spec);
	currentmapsection = sections;
	origin->flags &= ~MF_JUSTHIT;
	viewx = sx; viewy = sy; viewz = sz;
	viewangle = sa;
	viewsector = ssector;
	in_area = sarea;
	GLRenderer->mViewActor = sactor;
}

// [rc4l] GLSectorStackPortal::DrawContents's walk: from the eye moved by the portal's displacement, over the map sections its subsectors cover, the clipper opened only where its walls were seen from; then its actors to `sprite` from inside the view, and the skyboxes it looks into.
void WalkStack(const PortalView &p, void (*sprite)(AActor *thing))
{
	FPortal *origin = p.stack;
	const int plane = origin->plane;
	TArray<BYTE> sections(currentmapsection);
	if (currentmapsection.Size() > 0) memset(&currentmapsection[0], 0, currentmapsection.Size());
	g_covered.assign(numsubsectors, 0);
	for (size_t i = 0; i < p.subs.size(); i++)
	{
		const FPortalCoverage &cov = p.subs[i]->portalcoverage[plane];
		for (int j = 0; j < cov.sscount; j++)
		{
			const subsector_t *covered = &subsectors[cov.subsectors[j]];
			currentmapsection[covered->mapsection >> 3] |= 1 << (covered->mapsection & 7);
			g_covered[covered - subsectors] = 1;
		}
	}
	EnsureCoverageNodes();
	g_nodeCovered.assign(numnodes, 0);
	if (numnodes > 0 && numsubsectors > 0) ComputeNodeCoverage(&g_coverageNodes[0], numnodes - 1, &g_covered[0], &g_nodeCovered[0]);
	ComputeSeedPortal(g_clip, g_playerX, g_playerY, &p.lines[0], int(p.lines.size() / 4), 0, viewangle, GLRenderer->FrustumAngle());

	const fixed_t sx = viewx, sy = viewy;
	viewx += origin->xDisplacement;
	viewy += origin->yDisplacement;
	const int list = p.slot >= 0 ? zx::levelmesh::kSkyCaptureStack + p.slot : zx::levelmesh::kSkyCaptureNone;
	if (list != zx::levelmesh::kSkyCaptureNone) zx::levelmesh::BeginViewSky(list);
	std::vector<PortalView> skyboxes;
	const WalkSpec spec = { p.slot, list, false, NULL, &skyboxes, numsubsectors > 0 ? &g_covered[0] : NULL,
		numnodes > 0 ? &g_nodeCovered[0] : NULL };
	Walk(spec);

	// [rc4l] The view's actors, as GL's walk hands them to ProcessSprite, captured into the view's slot while its map sections still stand.
	if (sprite != NULL && p.slot >= 0)
	{
		zx::levelmesh::EnterScene();
		{
			const zx::levelmesh::StackWalk walk(origin);
			PortalSprites(sprite, NULL);
		}
		zx::levelmesh::LeaveScene();
	}
	const long long ex = viewx.Raw(), ey = viewy.Raw();
	for (size_t i = skyboxes.size(); i-- > 0; )
		if (!skyboxes[i].lines.empty()) WalkSkybox(skyboxes[i], ex, ey);
	currentmapsection = sections;
	viewx = sx;
	viewy = sy;
}

// [rc4l] GLMirrorPortal::DrawContents's walk: from the eye reflected in the line, turned to match, the frustum and the mirror's own span blocked and nothing behind the mirror walked; then its actors to `sprite` inside the reflection, and the skyboxes it looks into.
void WalkMirror(const PortalView &p, void (*sprite)(AActor *thing))
{
	line_t *line = p.mirror;
	const fixed_t sx = viewx, sy = viewy;
	const angle_t sa = viewangle;
	long long mx, my;
	ComputeMirrorEye(line->v1->x.Raw(), line->v1->y.Raw(), line->v2->x.Raw(), line->v2->y.Raw(), g_playerX, g_playerY, 1, mx, my);
	viewx = fixed_t::FromRaw(mx);
	viewy = fixed_t::FromRaw(my);
	viewangle = 2 * ComputePointToAnglePrecise(double((line->v2->x - line->v1->x).Raw()), double((line->v2->y - line->v1->y).Raw())) - sa;
	g_clip.Clear();
	const unsigned half = GLRenderer->FrustumAngle();
	if (half < 0x80000000u) ComputeSeedFrustum(g_clip, viewangle, half);
	g_clip.SafeAddClipRange(ComputePseudoAngle(double((line->v2->x - viewx).Raw()), double((line->v2->y - viewy).Raw())),
		ComputePseudoAngle(double((line->v1->x - viewx).Raw()), double((line->v1->y - viewy).Raw())));
	std::vector<PortalView> skyboxes;
	WalkSpec spec = { -1, zx::levelmesh::kSkyCaptureNone, false, NULL, &skyboxes, NULL, NULL, line };
	Walk(spec);
	if (sprite != NULL)
	{
		// [rc4l] Billboarded to the reflected view, as GL's SetupView turns its view vector for the mirror.
		GLRenderer->SetViewAngle(viewangle);
		zx::levelmesh::EnterScene();
		{
			const zx::levelmesh::MirrorWalk walk(int(line - lines));
			PortalSprites(sprite, line);
		}
		zx::levelmesh::LeaveScene();
		GLRenderer->SetViewAngle(sa);
	}
	const long long ex = viewx.Raw(), ey = viewy.Raw();
	for (size_t i = skyboxes.size(); i-- > 0; )
		if (!skyboxes[i].lines.empty()) WalkSkybox(skyboxes[i], ex, ey);
	viewx = sx;
	viewy = sy;
	viewangle = sa;
}

// [rc4l] The player's walk's marks against the bits GL's own walk left in the player's slot.
void CompareMain(const TArray<unsigned int> &glBits)
{
	g_checks++;
	int onlyGL = 0, onlyOurs = 0;
	const int ids = numsubsectors + numsides;
	const long long words = glBits.Size();
	for (int id = 0; id < ids; id++)
	{
		const long long bit = (long long)id * zx::levelmesh::kWalkSlots + zx::levelmesh::kMainWalkSlot;
		if (bit / 32 >= words) break;
		const bool gl = (glBits[unsigned(bit / 32)] >> (bit % 32)) & 1;
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

// [rc4l] The walk's sky surfaces and holes against GL's for the same frame, bit for bit.
void CheckSky(zx::levelmesh::MainSkyState &gl, zx::levelmesh::MainSkyState &ours)
{
	g_skyChecks++;
	const char *why = NULL;
	if (gl.quads.Size() != ours.quads.Size()) why = "sky quad count";
	else if (gl.quads.Size() && memcmp(&gl.quads[0], &ours.quads[0], gl.quads.Size() * sizeof(zx::levelmesh::MeshVertex))) why = "sky quad";
	else if (gl.holes.Size() != ours.holes.Size()) why = "sky hole count";
	else if (gl.holes.Size() && memcmp(&gl.holes[0], &ours.holes[0], gl.holes.Size() * sizeof(float))) why = "sky hole";
	else if (gl.viewpoint != ours.viewpoint) why = "skybox";
	if (!why) return;
	if (g_skyDiffers++ < 6)
		g_skyDiffs.AppendFormat("  %s: GL %u quads %u holes skybox %s, ours %u quads %u holes skybox %s\n", why, gl.quads.Size() / 6,
			gl.holes.Size() / zx::levelmesh::kSkyHoleFloats, gl.viewpoint ? gl.viewpoint->GetClass()->TypeName.GetChars() : "none",
			ours.quads.Size() / 6, ours.holes.Size() / zx::levelmesh::kSkyHoleFloats, ours.viewpoint ? ours.viewpoint->GetClass()->TypeName.GetChars() : "none");
}

bool SameVertices(const TArray<zx::levelmesh::MeshVertex> &a, const TArray<zx::levelmesh::MeshVertex> &b)
{
	return a.Size() == b.Size() && (a.Size() == 0 || memcmp(&a[0], &b[0], a.Size() * sizeof(zx::levelmesh::MeshVertex)) == 0);
}

// [rc4l] Whether two surfaces are shaded alike, by what CaptureShading sets.
bool SameShade(const zx::levelmesh::MeshPiece &a, const zx::levelmesh::MeshPiece &b)
{
	return a.colorR == b.colorR && a.colorG == b.colorG && a.colorB == b.colorB && a.softLight == b.softLight &&
		a.fogDensity == b.fogDensity && a.fogColor == b.fogColor && a.fogMode == b.fogMode && a.desaturation == b.desaturation;
}

// [rc4l] How many ids a walk slot marks in a set of bits.
int SlotCount(const TArray<unsigned int> &bits, int slot)
{
	int count = 0;
	const int ids = numsubsectors + numsides;
	for (int id = 0; id < ids; id++)
	{
		const unsigned bit = unsigned(id * zx::levelmesh::kWalkSlots + slot);
		if (bit / 32 < bits.Size() && ((bits[bit / 32] >> (bit % 32)) & 1)) count++;
	}
	return count;
}

// [rc4l] How many ids a walk slot marks in one set of bits and not the other.
int SlotDiffers(const TArray<unsigned int> &a, const TArray<unsigned int> &b, int slot)
{
	int differ = 0;
	const int ids = numsubsectors + numsides;
	for (int id = 0; id < ids; id++)
	{
		const unsigned bit = unsigned(id * zx::levelmesh::kWalkSlots + slot);
		const bool inA = bit / 32 < a.Size() && ((a[bit / 32] >> (bit % 32)) & 1);
		const bool inB = bit / 32 < b.Size() && ((b[bit / 32] >> (bit % 32)) & 1);
		if (inA != inB) differ++;
	}
	return differ;
}

} // namespace

void WalkView(bool publish, bool derive, void (*particles)(subsector_t *sub, sector_t *sector))
{
	const WalkSpec spec = { publish ? zx::levelmesh::kMainWalkSlot : -1, derive ? zx::levelmesh::kSkyCaptureMain : zx::levelmesh::kSkyCaptureNone,
		derive, zx::hwrender::SpriteView().ViewActor(), derive ? &g_playerPortals : NULL, NULL, NULL, NULL, particles };
	if (publish) zx::levelmesh::BeginSlotVisibility(zx::levelmesh::kMainWalkSlot);
	if (derive)
	{
		// [rc4l] A new frame of walks; at the wrap every stamp goes, so an old one cannot read as this frame's.
		if (++g_seenFrame == 0)
		{
			g_subSeen.assign(g_subSeen.size(), 0);
			g_sideSeen.assign(g_sideSeen.size(), 0);
			g_sectorSeen.assign(g_sectorSeen.size(), 0);
			g_seenFrame = 1;
		}
		g_seenSides.clear();
		g_seenSectors.clear();
		zx::levelmesh::BeginViewSky(zx::levelmesh::kSkyCaptureMain);
		zx::levelmesh::BeginPortalViews();
		g_playerPortals.clear();
		g_subProcessed.assign(numsubsectors, 0);
		g_secProcessed.assign(numsectors, 0);
		g_planeStacks[0].clear();
		g_planeStacks[1].clear();
		g_playerX = viewx.Raw();
		g_playerY = viewy.Raw();
	}
	g_clip.Clear();
	// [rc4l] GL's frustum, from its field of view and pitch, until the view's own lens moves here (P5).
	ComputeSeedFrustum(g_clip, viewangle, GLRenderer != NULL ? GLRenderer->FrustumAngle() : 0xffffffffu);
	Walk(spec);
	if (derive) ProcessSectorStacks();
	zx::levelmesh::SetMainVisibility(publish);
}

void WalkPortalViews(void (*sprite)(AActor *thing))
{
	if (GLRenderer == NULL) return;
	// [rc4l] Last made first, as GLPortal::EndFrame pops them; GL renders only a portal with lines.
	const std::vector<PortalView> portals(g_playerPortals);
	for (size_t i = portals.size(); i-- > 0; )
	{
		if (portals[i].lines.empty()) continue;
		if (portals[i].stack != NULL) WalkStack(portals[i], sprite);
		else if (portals[i].mirror != NULL)
		{
			zx::levelmesh::AddWalkedMirror(int(portals[i].mirror - lines));
			WalkMirror(portals[i], sprite);
		}
		else WalkSkybox(portals[i], g_playerX, g_playerY);
	}
}

const int *SectorsWalkedThisFrame(int &count)
{
	count = (int)g_seenSectors.size();
	return count ? &g_seenSectors[0] : NULL;
}

const int *SidesWalkedThisFrame(int &count)
{
	count = (int)g_seenSides.size();
	return count ? &g_seenSides[0] : NULL;
}

void ForgetWalk()
{
	g_subSeen.clear();
	g_sideSeen.clear();
	g_sectorSeen.clear();
	g_seenSides.clear();
	g_seenSectors.clear();
	g_subs.clear();
	g_sides.clear();
	g_sectors.clear();
	g_sectorOrder.clear();
	g_vertStamp.clear();
	g_vertAngle.clear();
	g_playerPortals.clear();
	g_coverageNodes.clear();
	g_subStamp.clear();
	g_oursReady = false;
}

void CheckAgainstGL()
{
	if (!fua_vis_check) return;
	// [rc4l] GL's sky and portal views set aside, ours derived into their place and taken out, GL's put back.
	zx::levelmesh::MainSkyState glSky, oursSky;
	zx::levelmesh::PortalViewState glPortals;
	zx::levelmesh::SwapMainSky(glSky);
	zx::levelmesh::SwapPortalViews(glPortals);
	WalkView(false, true);
	CompareMain(glPortals.bits);
	zx::levelmesh::SwapMainSky(oursSky);
	WalkPortalViews(NULL);
	g_oursPortals = zx::levelmesh::PortalViewState();
	zx::levelmesh::SwapPortalViews(g_oursPortals);
	zx::levelmesh::SwapPortalViews(glPortals);
	g_oursReady = true;
	CheckSky(glSky, oursSky);
	zx::levelmesh::SwapMainSky(glSky);
}

void CheckPortalViews()
{
	if (!g_oursReady) return;
	g_oursReady = false;
	zx::levelmesh::PortalViewState gl;
	zx::levelmesh::SwapPortalViews(gl);
	const zx::levelmesh::PortalViewState &ours = g_oursPortals;
	g_portalChecks++;
	g_stackViewsSeen += gl.stackUsed;
	g_mirrorsSeen += int(gl.mirrors.Size());
	g_sheensSeen += int(gl.sheens.Size());
	FString why;
	// [rc4l] GL's occlusion query can leave a view it judged hidden unwalked, which ours always walks; such a view is counted apart, not compared.
	bool unwalked = false;
	if (gl.stackUsed != ours.stackUsed) why.Format("stacked views: GL %d, ours %d", gl.stackUsed, ours.stackUsed);
	for (int i = 0; why.IsEmpty() && i < gl.stackUsed; i++)
	{
		if (gl.portals[i] != ours.portals[i] || gl.info[i].dx != ours.info[i].dx || gl.info[i].dy != ours.info[i].dy ||
			gl.info[i].ceiling != ours.info[i].ceiling) why.Format("stacked view %d: another portal", i);
		else if (!SameVertices(gl.walls[i], ours.walls[i]))
			why.Format("stacked view %d: walls, GL %u quads, ours %u", i, gl.walls[i].Size() / 6, ours.walls[i].Size() / 6);
		else if (SlotCount(gl.bits, i) == 0 && gl.sky[i].Size() == 0) unwalked = true;
		else if (!SameVertices(gl.sky[i], ours.sky[i]))
			why.Format("stacked view %d: sky, GL %u quads, ours %u", i, gl.sky[i].Size() / 6, ours.sky[i].Size() / 6);
		else
		{
			const int differ = SlotDiffers(gl.bits, ours.bits, i);
			if (differ) why.Format("stacked view %d: %d ids walked by one and not the other", i, differ);
		}
	}
	for (unsigned i = 0; why.IsEmpty() && i < gl.sheens.Size(); i++)
	{
		unsigned k = 0;
		while (k < ours.sheens.Size() && ours.sheens[k].line != gl.sheens[i].line) k++;
		if (k == ours.sheens.Size()) why.Format("mirror %d: GL's sheen, none of ours", gl.sheens[i].line);
		else if (!SameShade(gl.sheens[i].shade, ours.sheens[k].shade))
			why.Format("mirror %d: sheen light, GL %g, ours %g", gl.sheens[i].line, gl.sheens[i].shade.colorR, ours.sheens[k].shade.colorR);
	}
	if (why.IsEmpty() && gl.sheens.Size() != ours.sheens.Size())
		why.Format("mirror sheens: GL %u, ours %u", gl.sheens.Size(), ours.sheens.Size());
	for (unsigned i = 0; why.IsEmpty() && i < ours.mirrors.Size(); i++)
	{
		unsigned k = 0;
		while (k < gl.mirrors.Size() && gl.mirrors[k] != ours.mirrors[i]) k++;
		if (k == gl.mirrors.Size()) unwalked = true;
	}
	for (unsigned i = 0; why.IsEmpty() && i < gl.mirrors.Size(); i++)
	{
		unsigned k = 0;
		while (k < ours.mirrors.Size() && ours.mirrors[k] != gl.mirrors[i]) k++;
		if (k == ours.mirrors.Size()) why.Format("mirror %d: walked by GL, not ours", gl.mirrors[i]);
	}
	if (why.IsEmpty())
	{
		const int differ = SlotDiffers(gl.bits, ours.bits, zx::levelmesh::kSkyboxWalkSlot);
		if (!gl.skyboxWalked && ours.skyboxWalked) unwalked = true;
		else if (gl.skyboxWalked != ours.skyboxWalked || differ)
			why.Format("skybox walk: GL %s, ours %s, %d ids walked by one and not the other", gl.skyboxWalked ? "walked" : "none",
				ours.skyboxWalked ? "walked" : "none", differ);
	}
	zx::levelmesh::SwapPortalViews(gl);
	if (unwalked && why.IsEmpty()) g_portalUnwalked++;
	if (why.IsEmpty()) return;
	if (g_portalDiffers++ < 6) g_portalDiffs.AppendFormat("  %s\n", why.GetChars());
}

}} // namespace zx::visibility

CCMD( fua_vis_report )
{
	Printf( "visibility: %d frames walked beside GL, %d differed (%d ids only GL reached, %d only ours)\n%s",
		zx::visibility::g_checks, zx::visibility::g_checkDiffers, zx::visibility::g_onlyGL, zx::visibility::g_onlyOurs,
		zx::visibility::g_diffs.GetChars( ) );
	Printf( "sky: %d frames derived beside GL, %d differed\n%s", zx::visibility::g_skyChecks, zx::visibility::g_skyDiffers,
		zx::visibility::g_skyDiffs.GetChars( ) );
	Printf( "portal views: %d frames derived beside GL (%d stacked views, %d mirrors, %d sheens), %d differed, %d with a view GL's occlusion query left unwalked\n%s",
		zx::visibility::g_portalChecks, zx::visibility::g_stackViewsSeen, zx::visibility::g_mirrorsSeen, zx::visibility::g_sheensSeen,
		zx::visibility::g_portalDiffers, zx::visibility::g_portalUnwalked, zx::visibility::g_portalDiffs.GetChars( ) );
	zx::visibility::g_skyChecks = zx::visibility::g_skyDiffers = 0;
	zx::visibility::g_skyDiffs = "";
	zx::visibility::g_portalChecks = zx::visibility::g_portalDiffers = zx::visibility::g_portalUnwalked = 0;
	zx::visibility::g_stackViewsSeen = zx::visibility::g_mirrorsSeen = zx::visibility::g_sheensSeen = 0;
	zx::visibility::g_portalDiffs = "";
	zx::visibility::g_checks = zx::visibility::g_checkDiffers = zx::visibility::g_onlyGL = zx::visibility::g_onlyOurs = 0;
	zx::visibility::g_diffs = "";
}
