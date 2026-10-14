// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/visibility/viswalk.h"
#include "features/frame/computation/oracle_compute.h"   // [rc4l] OracleTally

#include "features/visibility/computation/clipranges_compute.h"
#include "features/visibility/computation/closedline_compute.h"
#include "features/visibility/computation/coverage_compute.h"
#include "features/hwrender/computation/skylook_compute.h"   // [rc4l] ComputeSkyQuadKey, which sky a sky wall shows
#include "features/visibility/computation/mirrorview_compute.h"
#include "features/visibility/computation/walkslices_compute.h"
#include "features/jobs/jobs.h"   // [rc4l] the pool a sliced walk runs on
#include "features/jobs/computation/sharedarray_compute.h"   // [rc4l] the marks a walk's lanes share
#include "features/levelmesh/wallcache.h"   // [rc4l] the backend's walk bits and sky surfaces
#include "features/levelmesh/flatmesh.h"    // [rc4l] the stacked views, and the scene nesting a stacked view's sprites are captured in
#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/fakeplanes.h"   // [rc4l] FakePlanesFor, a fill's model sector
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] the walk slots, the sky lists and a sky hole's size
#include "features/surfaces/computation/skywall_compute.h"
#include "features/surfaces/computation/wallgeom_compute.h"   // [rc4l] where a pinched wall ends, as SetWallCoordinates moves it
#include "features/materials/materials.h"     // [rc4l] whether a one-sided wall has a texture, as GL asks
#include "features/hwrender/spriteview.h"   // [rc4l] the sprite view inside a portal view
#include "features/hwrender/hud2d.h"        // [rc4l] SetSkyFog
#include "features/hwrender/computation/skyfog_compute.h"
#include "features/viewsetup/viewsetup.h"    // [rc4l] the view's actor and frustum
#include "features/fixed64/computation/angle_interp_compute.h"   // [rc4l] a skybox's turn, as GL interpolates it
#include "g_shared/a_sharedglobal.h"        // [rc4l] ASkyViewpoint
#include "p_lnspec.h"                       // [rc4l] Line_Horizon
#include "r_defs.h"                         // [rc4l] secplane_t, which p_3dfloors.h uses without including
#include "p_3dfloors.h"                     // [rc4l] F3DFloor, a 3D floor's control sector
#include "c_console.h"                      // [rc4l] C_ShouldForceInterpolation
#include "p_local.h"                        // [rc4l] P_PointOnLineSide, a mirror's clip
#include "p_effect.h"                       // [rc4l] ParticlesInSubsec, which subsectors have particles to hand over
#include "po_man.h"                         // [rc4l] polyobjects, whose BSPs a sliced walk builds first

#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"
#include "r_utility.h"
#include "textures/textures.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "stats.h"   // [rc4l] cycle_t, to time the player's walks

// [rc4l] GL's fake flats, view area and portal coverage, which decide what a height transfer hides and where a stacked view starts, until they are ported (P5).
#include "features/fakeflat/fakeflat.h"   // [rc4l] gl_FakeFlat
#include "features/stackportals/stackportals.h"   // [rc4l] FPortal, portals, gl_BuildPortalCoverage
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads

#include <string.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <vector>

// [rc4l] Compare the walk against GL's own every frame GL walks, counting what differs for fua_vis_report.
CVAR(Bool, fua_vis_check, false, 0)
// [rc4l] Slices the player's walk is split into by view angle and run on the job pool: -1 is one a lane, 1 walks it on the game thread alone.
CVAR(Int, fua_vis_slices, -1, 0)
// [rc4l] Walk the player's view in one slice beside the sliced walk and compare the two, for fua_vis_report.
CVAR(Bool, fua_vis_threadcheck, false, 0)
// [rc4l] Move the player's walk's slice edges walk to walk so each slice has about the same work.
CVAR(Bool, fua_vis_balance, true, 0)
EXTERN_CVAR(Bool, r_noskyboxes)
EXTERN_CVAR(Bool, r_mirrors)
EXTERN_CVAR(Bool, r_mirror_envmap)

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
// [rc4l] The clipper a walk starts from, which each of its slices copies.
ClipRanges g_clip;
const int kMaxSlices = 64;
// [rc4l] The player's walk's slice edges as fractions of the open view, and the work each slice did last walk.
std::vector<double> g_sliceEdges, g_sliceNext;
std::vector<long long> g_sliceWork;
// [rc4l] Sectors in the order the walk first reached them, as GL's RenderThings takes them.
std::vector<sector_t *> g_sectorOrder;
long long g_viewx = 0, g_viewy = 0;
// [rc4l] Every walk's subsectors, sidedefs and sectors since the player's last derived walk, stamped with that walk's frame, and the sidedefs and sectors as lists in index order, made when first asked for.
zx::jobs::SharedArray<unsigned> g_subSeen, g_sideSeen, g_sectorSeen;
std::vector<int> g_seenSides, g_seenSectors;
// [rc4l] The sidedefs with decals the last walk saw, each once, in index order.
std::vector<int> g_decalSides;
unsigned g_seenFrame = 1;
bool g_seenListsReady = false;
// [rc4l] Linedefs whose walls this walk has built, since GL builds a line's once a walk from its first visible seg.
std::vector<unsigned char> g_lineDone;
const int kNoSkyDraw = 89;   // [rc4l] gl_sky.cpp's NoSkyDraw sector special

// [rc4l] The player's walk's flats pass: subsectors GL processed (SSRF_PROCESSED), render sectors processed, and the stacked planes facing the eye.
zx::jobs::SharedArray<unsigned char> g_subProcessed;
std::vector<unsigned char> g_secProcessed;
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

// [rc4l] What the player's walks cost since the last report, the main view's and its portal views' apart, since a frame capped by vsync hides both.
int g_timedWalks = 0;
double g_walkMs = 0, g_walkMaxMs = 0, g_portalWalkMs = 0;
// [rc4l] The clipping half of every walk, sliced or not, and how many slices the player's walks ran in.
double g_clipMs = 0;
long long g_walkSlices = 0;
// [rc4l] And how much they reached: subsectors, segs, segs facing the eye, and segs the clipper let through.
long long g_walkSubs = 0, g_walkSegs = 0, g_walkFacing = 0, g_walkOpen = 0;
zx::frame::OracleTally g_walkCheck, g_skyCheck, g_portalCheck;   // [rc4l] the player's walk, its sky, and the portal views, each against GL's
zx::frame::OracleTally g_sliceCheck;   // [rc4l] the sliced walk against one slice
int g_onlyGL = 0, g_onlyOurs = 0, g_portalUnwalked = 0;
// [rc4l] What the portal-view check had to compare, summed over its frames.
int g_stackViewsSeen = 0, g_mirrorsSeen = 0, g_sheensSeen = 0;
// [rc4l] The stacked views and skybox walks derived beside GL's walk, held until GL has walked its own portals.
zx::levelmesh::PortalViewState g_oursPortals;
bool g_oursReady = false;

bool HasTexture(side_t *side, int part)
{
	FTexture *tex = TexMan(side->GetTexture(part));
	return tex != NULL && tex->UseType != FTexture::TEX_Null;
}

// [rc4l] A sector whose change re-bakes something a walk reached, and the control sectors of the 3D floors in it, which re-bake it with them.
void SeeSector(const sector_t *s)
{
	if (s == NULL) return;
	const int i = s->sectornum;
	if (g_sectorSeen.Load(i) == g_seenFrame) return;
	g_sectorSeen.Store(i, g_seenFrame);
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
			const long long h = ComputeFlatPlaneHeight(p == 0 || p == 2, plane.d.Raw());
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

// [rc4l] Whether a sector's height transfer is in force, as gl_FakeFlat and gl_CheckViewArea ask.
bool TransfersHeight(const sector_t *s)
{
	return s->heightsec != NULL && !(s->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC);
}

// [rc4l] Whether a seg leads from the open into a height transfer in force, the line gl_CheckViewArea decides the view area by.
bool EntersTransfer(const seg_t *seg)
{
	return TransfersHeight(seg->backsector) && !TransfersHeight(seg->frontsector);
}

// [rc4l] The view area such a seg decides: below the water when the front ceiling sits at or under the transfer's floor.
area_t AreaAcross(const seg_t *seg)
{
	const sector_t *front = seg->frontsector, *s = seg->backsector->heightsec;
	return ComputeEyeBelowTransfer(front->ceilingplane.ZatPoint(seg->v1).Raw(), front->ceilingplane.ZatPoint(seg->v2).Raw(),
		s->floorplane.ZatPoint(seg->v1).Raw(), s->floorplane.ZatPoint(seg->v2).Raw()) ? area_below : area_normal;
}

// [rc4l] gl_CheckViewArea on a walk's own view area: the first line from the open into a height transfer decides whether the eye is below its water.
void CheckViewArea(area_t &area, const seg_t *seg)
{
	if (area == area_default && EntersTransfer(seg)) area = AreaAcross(seg);
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

// [rc4l] One thing DeriveSkyWalls makes of a seg, worked out on a lane and applied in walk order: a stacked plane's wall, a sky wall, a sky hole, or a mirror and its sheen.
struct SkyEvent
{
	enum Kind { kStackWall, kSkyWall, kSkyHole, kMirror };
	int kind;
	float x1, y1, x2, y2;
	float zb[2], zt[2];      // [rc4l] a wall's bottom and top, or a hole's floor and ceiling
	FPortal *portal;         // [rc4l] a stacked wall's portal
	ASkyViewpoint *skybox;   // [rc4l] the skybox a sky wall shows, or NULL
	line_t *mirror;
	int special;             // [rc4l] a sky wall's line special
	bool sheen;              // [rc4l] whether a mirror's sheen is drawn, in `light` and `cm`
	int light;
	FColormap cm;
	unsigned fade;           // [rc4l] a sky wall's sector fade colour, which GLWall::SkyPlane gives its sky
	int sky;                 // [rc4l] a sky wall's sector's sky (sector_t::sky), which picks the sky it shows
};

SkyEvent &PushSkyEvent(std::vector<SkyEvent> &out, int kind, float x1, float y1, float x2, float y2, const float *zb, const float *zt)
{
	out.push_back(SkyEvent());
	SkyEvent &e = out.back();
	e.kind = kind;
	e.x1 = x1; e.y1 = y1; e.x2 = x2; e.y2 = y2;
	e.zb[0] = zb[0]; e.zb[1] = zb[1];
	e.zt[0] = zt[0]; e.zt[1] = zt[1];
	return e;
}

// [rc4l] GLWall::SkyPlane: a stacked plane's wall into its view, a sky's into the walk's sky list with the skybox it names, and either's line into its portal.
void SkyPlane(sector_t *sector, int plane, float x1, float y1, float x2, float y2, const zx::surfaces::SkyWallQuad &q, int special, std::vector<SkyEvent> &out)
{
	FPortal *portal = sector->portals[plane];
	if (portal != NULL)
	{
		// [rc4l] Only the player's walk's stacked walls are captured, as AddStackWall takes only its.
		if (g_spec.player) PushSkyEvent(out, SkyEvent::kStackWall, x1, y1, x2, y2, q.zbottom, q.ztop).portal = portal;
		return;
	}
	if (sector->GetTexture(plane) != skyflatnum) return;
	ASkyViewpoint *skybox = sector->GetSkyBox(plane);
	if (r_noskyboxes || skybox == NULL || g_spec.viewActor == skybox || (skybox->flags & MF_JUSTHIT)) skybox = NULL;
	SkyEvent &e = PushSkyEvent(out, SkyEvent::kSkyWall, x1, y1, x2, y2, q.zbottom, q.ztop);
	e.skybox = skybox;
	e.special = special;
	e.fade = sector->ColorMap != NULL ? sector->ColorMap->Fade.d & 0xffffff : 0;
	e.sky = sector->sky;
}

// [rc4l] The fade of the first sky the player's walk met, whose portal GL makes first and so draws last, leaving its fog on the sky.
bool g_skyFadeSeen = false;
unsigned g_skyFade = 0;

// [rc4l] A sky event into the walk's sky list, portals and mirrors, in the order the walk made it.
void ApplySkyEvent(const SkyEvent &e)
{
	switch (e.kind)
	{
	case SkyEvent::kStackWall:
	{
		PortalView &p = PortalFor(e.portal, NULL);
		const int slot = zx::levelmesh::AddStackWall(e.portal, e.x1, e.y1, e.x2, e.y2, e.zb, e.zt);
		if (slot >= 0) p.slot = slot;
		AddPortalLine(p, e.x1, e.y1, e.x2, e.y2);
		break;
	}
	case SkyEvent::kSkyWall:
		if (e.skybox != NULL && g_spec.portals != NULL) AddPortalLine(PortalFor(NULL, e.skybox), e.x1, e.y1, e.x2, e.y2);
		if (g_spec.player && e.skybox == NULL && !g_skyFadeSeen) { g_skyFadeSeen = true; g_skyFade = e.fade; }
		zx::levelmesh::AddViewSkyQuad(g_spec.skyList, e.x1, e.y1, e.x2, e.y2, e.zb, e.zt, e.skybox, e.special, zx::hwrender::ComputeSkyQuadKey(e.sky));
		break;
	case SkyEvent::kSkyHole:
		zx::levelmesh::AddViewSkyHole(g_spec.skyList, e.x1, e.y1, e.x2, e.y2, e.zb, e.zt);
		break;
	default:
		AddPortalLine(PortalFor(NULL, NULL, e.mirror), e.x1, e.y1, e.x2, e.y2);
		if (e.sheen) zx::levelmesh::AddMirrorSheen(int(e.mirror - lines), e.light, e.cm);
		break;
	}
}

// [rc4l] A one-sided Line_Mirror wall the player's walk reached, as GLWall::DoTexture and PutWall take it: its portal, and its sheen in the light of the first piece SplitWall puts.
void NoteMirror(seg_t *seg, sector_t *fs, subsector_t *sub, float x1, float y1, float x2, float y2, const float *zfloor, const float *zceil,
	std::vector<SkyEvent> &out)
{
	// [rc4l] GLWall::Process's light for the wall, before any light band.
	FColormap cm;
	cm = fs->ColorMap;
	const int baseLight = gl_ClampLight(fs->lightlevel);
	const bool foggy = !gl_isBlack(cm.FadeColor) || (level.flags & LEVEL_HASFADETABLE);
	int rel = 0;
	int light = gl_ClampLight(seg->sidedef->GetLightLevel(foggy, baseLight, false, &rel));
	TArray<lightlist_t> &bands = sub->sector->e->XFloor.lightlist;
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
	SkyEvent &e = PushSkyEvent(out, SkyEvent::kMirror, x1, y1, x2, y2, zfloor, zceil);
	e.mirror = seg->linedef;
	if (!r_mirror_envmap) return;
	if (gl_fixedcolormap) cm.Clear();
	if (band >= 0)
	{
		// [rc4l] GLWall::Put3DWall's light for the band.
		const lightlist_t &l = bands[band];
		const bool fadewall = l.caster && (l.caster->flags & FF_FADEWALLS) && !gl_isBlack(l.extra_colormap->Fade) && gl_isBlack(cm.FadeColor);
		const bool own = l.p_lightlevel != &seg->sidedef->sector->lightlevel;
		light = ComputeSheenBandLight(light, own, own ? gl_ClampLight(*l.p_lightlevel) : light, fadewall);
		cm.CopyLightColor(l.extra_colormap);
	}
	e.sheen = true;
	e.light = light;
	e.cm = cm;
}

// [rc4l] The sky and stacked walls GLWall::Process raises from this seg in subsector `sub`, from front sector `fs` and back `bs` as GL's walk hands them over, and a mirror's portal, as events into `out`.
void DeriveSkyWalls(seg_t *seg, sector_t *fs, sector_t *bs, subsector_t *sub, std::vector<SkyEvent> &out)
{
	const int special = seg->linedef->special;
	const bool oneSided = special == Line_Horizon || !bs || !(seg->linedef->flags & (ML_TWOSIDED | ML_3DMIDTEX));
	// [rc4l] SkyPlane makes something only of a sky or a stacked plane, so a plane that is neither raises nothing, and with neither, no hole and no mirror the seg is done.
	const bool ceilingOut = fs->GetTexture(sector_t::ceiling) == skyflatnum || fs->portals[sector_t::ceiling] != NULL;
	const bool floorOut = fs->GetTexture(sector_t::floor) == skyflatnum || fs->portals[sector_t::floor] != NULL;
	// [rc4l] An untextured one-sided wall is a hole GL's sky fills.
	const bool hole = oneSided && special != Line_Horizon && !HasTexture(seg->sidedef, side_t::mid);
	const bool mirror = oneSided && special == Line_Mirror && !hole && g_spec.player && r_mirrors;
	if (!ceilingOut && !floorOut && !hole && !mirror) return;

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

	if (oneSided)
	{
		zx::surfaces::SkyWallQuad q[2];
		zx::surfaces::ComputeSkyNormal(zfloor, zceil, q);
		SkyPlane(fs, sector_t::ceiling, x1, y1, x2, y2, q[0], special, out);
		SkyPlane(fs, sector_t::floor, x1, y1, x2, y2, q[1], special, out);
		if (special == Line_Horizon)
		{
			const int n = zx::surfaces::ComputeHorizonSky(viewz.Raw(), fs->GetPlaneTexZ(sector_t::ceiling).Raw(), fs->GetPlaneTexZ(sector_t::floor).Raw(),
				fs->GetTexture(sector_t::ceiling) == skyflatnum, fs->GetTexture(sector_t::floor) == skyflatnum, q);
			for (int i = 0; i < n; i++) SkyPlane(fs, q[i].ceiling ? sector_t::ceiling : sector_t::floor, x1, y1, x2, y2, q[i], special, out);
		}
		else if (hole) PushSkyEvent(out, SkyEvent::kSkyHole, x1, y1, x2, y2, zfloor, zceil);
		else if (mirror) NoteMirror(seg, fs, sub, x1, y1, x2, y2, zfloor, zceil, out);
		return;
	}
	// [rc4l] The back planes at the line's ends, read once for SkyTop and SkyBottom both.
	const fixed_t backCeil1 = bs->ceilingplane.ZatPoint(v1), backCeil2 = bs->ceilingplane.ZatPoint(v2);
	const fixed_t backFloor1 = bs->floorplane.ZatPoint(v1), backFloor2 = bs->floorplane.ZatPoint(v2);
	const bool backOpen = backCeil1 > backFloor1 || backCeil2 > backFloor2;
	const bool backNoSkyDraw = (bs->special & 0xff) == kNoSkyDraw;
	zx::surfaces::SkyWallQuad q;
	if (ceilingOut)
	{
		zx::surfaces::SkyTopIn t = {};
		t.frontSky = fs->GetTexture(sector_t::ceiling) == skyflatnum;
		t.backSky = bs->GetTexture(sector_t::ceiling) == skyflatnum;
		t.backNoSkyDraw = backNoSkyDraw;
		t.backOpen = backOpen || bs->transdoor;
		t.icarus = bs->floorplane.a == 0 && bs->floorplane.b == 0 && fs->floorplane.a == 0 && fs->floorplane.b == 0 &&
			bs->GetPlaneTexZ(sector_t::floor) == fs->GetPlaneTexZ(sector_t::floor) + FRACUNIT;
		// [rc4l] SkyTop reads the side's textures only for Icarus's door, so their lookups wait for one.
		if (t.icarus)
		{
			t.hasBottom = HasTexture(seg->sidedef, side_t::bottom);
			t.midExists = TexMan(seg->sidedef->GetTexture(side_t::mid)) != NULL;
			t.dontPegTop = (seg->linedef->flags & ML_DONTPEGTOP) != 0;
			t.midOffsetDown = seg->sidedef->GetTextureYOffset(side_t::mid) > 0;
			t.backCeilingPlusMid = FIXED2FLOAT(backCeil2 + seg->sidedef->GetTextureYOffset(side_t::mid));
		}
		t.zceil[0] = zceil[0]; t.zceil[1] = zceil[1];
		t.backCeiling[0] = FIXED2FLOAT(backCeil1);
		t.backCeiling[1] = FIXED2FLOAT(backCeil2);
		t.frontReflect = fs->GetReflect(sector_t::ceiling) > 0;
		t.backReflectSameHeight = bs->GetReflect(sector_t::ceiling) > 0 && bs->ceilingplane.d == fs->ceilingplane.d;
		t.frontPortal = fs->portals[sector_t::ceiling] != NULL;
		t.samePortal = fs->portals[sector_t::ceiling] == bs->portals[sector_t::ceiling];
		if (zx::surfaces::ComputeSkyTop(t, q)) SkyPlane(fs, sector_t::ceiling, x1, y1, x2, y2, q, special, out);
	}
	if (floorOut)
	{
		zx::surfaces::SkyBottomIn b;
		b.frontSky = fs->GetTexture(sector_t::floor) == skyflatnum;
		b.backSky = bs->GetTexture(sector_t::floor) == skyflatnum;
		b.backNoSkyDraw = backNoSkyDraw;
		b.bottomNull = !HasTexture(seg->sidedef, side_t::bottom);
		b.backOpen = backOpen;
		b.eyeBelowBackFloor = bs->floorplane.ZatPoint(FIXED2FLOAT(viewx), FIXED2FLOAT(viewy)) > FIXED2FLOAT(viewz);
		b.zfloor[0] = zfloor[0]; b.zfloor[1] = zfloor[1];
		b.backFloor[0] = FIXED2FLOAT(backFloor1);
		b.backFloor[1] = FIXED2FLOAT(backFloor2);
		b.frontReflect = fs->GetReflect(sector_t::floor) > 0;
		b.backReflectSameHeight = bs->GetReflect(sector_t::floor) > 0 && bs->floorplane.d == fs->floorplane.d;
		b.frontPortal = fs->portals[sector_t::floor] != NULL;
		b.samePortal = fs->portals[sector_t::floor] == bs->portals[sector_t::floor];
		if (zx::surfaces::ComputeSkyBottom(b, q)) SkyPlane(fs, sector_t::floor, x1, y1, x2, y2, q, special, out);
	}
}

// [rc4l] GLMirrorPortal::ClipSeg and ClipSubsector: in a mirror's walk, whatever lies wholly behind the mirror.
bool BehindMirror(const vertex_t *v) { return P_PointOnLineSide(v->x, v->y, g_spec.mirror) != 0; }

bool SubsectorBehindMirror(subsector_t *sub)
{
	for (DWORD i = 0; i < sub->numlines; i++)
		if (!BehindMirror(sub->firstline[i].v1)) return false;
	return true;
}

// [rc4l] What a walk keeps per subsector it reached beyond reaching it: particles to hand over, and a flats pass that must run in walk order.
enum { kSubParticles = 1, kSubFlats = 2 };

// [rc4l] A seg DeriveSkyWalls built something of, kept by the slice that saw it: its place in the walk, and its events in the slice's list.
struct DeriveCandidate
{
	unsigned long long key;
	seg_t *seg;
	int first, count;
};

// [rc4l] One slice of a walk: a clipper open only over its share of the view, its own view area, the ranks it reached in order when it is the walk's only slice, its sky-wall candidates in walk order and their events, and what it counted.
struct Slice
{
	ClipRanges clip;
	area_t area;
	std::vector<int> ranks;
	std::vector<DeriveCandidate> derive;
	std::vector<SkyEvent> events;
	size_t cursor;
	long long tried, facing, open;
	// [rc4l] The first line into a height transfer it saw, by walk key, which decides the walk's view area.
	unsigned long long firstTransfer;
	seg_t *firstTransferSeg;
	// [rc4l] The flag fields of lines to map, and of subsectors and sectors to mark seen, applied once the walk is final.
	std::vector<DWORD *> mapLines;
	std::vector<int *> drawnSubs;
	std::vector<WORD *> drawnSectors;
	// [rc4l] The node children it passed over though a seg under them showed in it, and those it entered only because it was told to.
	std::vector<LeakBox> leaks;
	// [rc4l] The sidedefs it saw that carry decals, each as often as a seg of it showed.
	std::vector<int> decalSides;
};

// [rc4l] Each vertex's pseudo-angle on one lane this walk, stamped with the walk, the stamp beside the angle so a vertex costs one cache miss.
struct VertAngle { unsigned stamp, angle; };
std::vector<std::vector<VertAngle> > g_laneAngles;
std::vector<Slice> g_slices;
// [rc4l] Per node its subsector count, which numbers a subsector by its place in the front-to-back order, and per subsector its sector.
std::vector<int> g_leaves, g_subSector;
// [rc4l] Per node and per subsector the node above it and which of its children it is (parent * 2 + child), -1 at the root.
std::vector<int> g_nodeUp, g_subUp;
// [rc4l] The seg the player's last sliced walk switched its view area at, guessed to be where this one does.
seg_t *g_lastTransfer = NULL;
int g_transferReruns = 0;
// [rc4l] Per rank the subsector there and the walk that last reached it; per subsector that walk, its flags and its view areas; per sidedef the walk that last saw it.
zx::jobs::SharedArray<unsigned> g_rankWalked, g_subWalked, g_sideWalked;
// [rc4l] The walk a rank last had a sky or stacked wall candidate in, so the replay skips the ranks that have none without asking every slice.
zx::jobs::SharedArray<unsigned> g_rankDerived;
zx::jobs::SharedArray<int> g_rankSub;
zx::jobs::SharedArray<unsigned char> g_subFlags, g_subAreaIn, g_subAreaOut;
// [rc4l] Per linedef the walk key of the first seg a deriving walk saw of it, under the walk's epoch: GL builds a line's walls from that seg alone.
zx::jobs::SharedArray<unsigned long long> g_lineFirst;
unsigned g_walkStamp = 0;
// [rc4l] Per node child (node * 2 + child) the segs under it with an end past its box (ComputeLeakySegs), whether it has any, whether slices enter it whatever the clipper says, and the walk that last entered it by its own test; and the children forced now.
std::vector<int> g_leakStart, g_leakSegs;
std::vector<unsigned char> g_boxWatch, g_boxForced;
zx::jobs::SharedArray<unsigned> g_boxEntered;
std::vector<int> g_forcedNow;
int g_leakReruns = 0;

// [rc4l] Per sector the walk that last put it in g_sectorOrder.
std::vector<unsigned> g_sectorWalked;
// [rc4l] Each polyobject subsector's segs numbered in the order its BSP is walked front to back from this walk's eye, so every slice keys them alike.
std::map<int, std::vector<unsigned short> > g_polyPos;
// [rc4l] The ids a sliced walk marked in its walk bits, for the slot's next clear.
std::vector<int> g_published;
// [rc4l] The level the arrays above were sized for.
int g_arraysFor[6] = { -1, -1, -1, -1, -1, -1 };

const int kKeyEpochShift = 40;
const unsigned long long kKeyEpochMask = 0xffffffull << kKeyEpochShift;

// [rc4l] A seg's place in a walk: the walk's epoch, its subsector's rank, and its place in the subsector.
unsigned long long WalkKey(int rank, int pos)
{
	return ((unsigned long long)(g_walkStamp & 0xffffff) << kKeyEpochShift) | ((unsigned long long)rank << 16) | (unsigned)pos;
}

int KeyRank(unsigned long long key) { return int((key >> 16) & 0xffffff); }

// [rc4l] Set a flag in an engine field now, or, from a lane, keep the field for after the walk; one already set costs a read.
template <class T, class U> void SetFlag(T &field, U bit, bool direct, std::vector<T *> &later)
{
	if (field & bit) return;
	if (direct) field = T(field | bit);
	else later.push_back(&field);
}

// [rc4l] A slice's walk on one lane: the slice, the lane's vertex angles, and whether it is the walk's only slice, on the game thread, which marks what it sees itself and keeps its own view area; slices instead switch from `initial` to `switchTo` at `switchKey`, where the walk found its first line into a height transfer.
struct ClipWalk
{
	Slice *slice;
	VertAngle *angles;
	bool direct;
	unsigned long long switchKey;
	area_t initial, switchTo;
};

unsigned ClipAngle(const ClipWalk &w, vertex_t *v)
{
	const ptrdiff_t i = v - vertexes;
	if (i < 0 || i >= numvertexes)
		return ComputePseudoAngle((double)(v->x - g_viewx).Raw(), (double)(v->y - g_viewy).Raw());
	VertAngle &va = w.angles[i];
	if (va.stamp != g_walkStamp)
	{
		va.stamp = g_walkStamp;
		va.angle = ComputePseudoAngle((double)(v->x - g_viewx).Raw(), (double)(v->y - g_viewy).Raw());
	}
	return va.angle;
}

bool BoxVisible(const ClipRanges &clip, const fixed_t *bbox)
{
	const long long box[4] = { bbox[0].Raw(), bbox[1].Raw(), bbox[2].Raw(), bbox[3].Raw() };
	return ComputeBoxVisible(clip, g_viewx, g_viewy, box);
}

// [rc4l] Whether DeriveSkyWalls makes anything of this seg seen from front sector `fs`: a sky or stacked plane on that side, an untextured one-sided wall, or the player's mirror.
bool SkyWallsPossible(seg_t *seg, sector_t *fs)
{
	if (fs->GetTexture(sector_t::ceiling) == skyflatnum || fs->portals[sector_t::ceiling] != NULL ||
		fs->GetTexture(sector_t::floor) == skyflatnum || fs->portals[sector_t::floor] != NULL) return true;
	const int special = seg->linedef->special;
	const bool oneSided = special == Line_Horizon || !seg->backsector || !(seg->linedef->flags & (ML_TWOSIDED | ML_3DMIDTEX));
	if (!oneSided || special == Line_Horizon) return false;
	return !HasTexture(seg->sidedef, side_t::mid) || (special == Line_Mirror && g_spec.player && r_mirrors);
}

// [rc4l] A deriving walk saw a seg of `line` at `key`; the line keeps the earliest.
void NoteLineSeg(int line, unsigned long long key, bool direct)
{
	if (!direct)
	{
		g_lineFirst.LowerWithin(line, key, kKeyEpochMask);
		return;
	}
	const unsigned long long old = g_lineFirst.Load(line);
	if (((old ^ key) & kKeyEpochMask) != 0 || key < old) g_lineFirst.Store(line, key);
}

// [rc4l] AddLine: whether the slice sees the seg, what a seen one hides, and what seeing it marks; `polyPass` is the polyobjects' own BSP, the level's segs leaving a polyobject's sides to it as GL's DoSubsector does.
void ClipSeg(ClipWalk &w, seg_t *seg, bool polyPass, subsector_t *sub, sector_t *front, int rank, int pos, bool &drawn)
{
	if (g_spec.mirror != NULL && BehindMirror(seg->v1) && BehindMirror(seg->v2)) return;
	const unsigned startAngle = ClipAngle(w, seg->v2);
	const unsigned endAngle = ClipAngle(w, seg->v1);
	Slice &slice = *w.slice;
	slice.tried++;
	// [rc4l] Backface culling: GL draws a seg only from its front.
	if (startAngle - endAngle < 0x80000000u) return;
	slice.facing++;
	if (!polyPass && seg->sidedef != NULL && (seg->sidedef->Flags & WALLF_POLYOBJ)) return;
	if (seg->sidedef == NULL)
	{
		if (!drawn && slice.clip.SafeCheckRange(startAngle, endAngle)) { drawn = true; SetFlag(sub->flags, SSECF_DRAWN, w.direct, slice.drawnSubs); }
		return;
	}
	if (!slice.clip.SafeCheckRange(startAngle, endAngle)) return;
	slice.open++;
	if (!drawn) { drawn = true; SetFlag(sub->flags, SSECF_DRAWN, w.direct, slice.drawnSubs); }
	const int side = int(seg->sidedef - sides);
	g_sideWalked.Store(side, g_walkStamp);
	if (seg->sidedef->AttachedDecals != NULL) slice.decalSides.push_back(side);
	if (g_sideSeen.Load(side) != g_seenFrame)
	{
		g_sideSeen.Store(side, g_seenFrame);
		SeeSector(seg->frontsector);
		SeeSector(seg->backsector);
	}
	if (w.direct && g_spec.slot >= 0) zx::levelmesh::NoteSlotVisible(g_spec.slot, numsubsectors + side);
	const bool polyobj = (seg->sidedef->Flags & WALLF_POLYOBJ) != 0;
	const unsigned long long key = WalkKey(rank, pos);
	sector_t bs;
	sector_t *back = NULL;
	if (!seg->backsector) slice.clip.SafeAddClipRange(startAngle, endAngle);
	else if (polyobj || front->sectornum == seg->backsector->sectornum) back = front;
	else
	{
		if (w.direct) CheckViewArea(slice.area, seg);
		else
		{
			if (w.initial == area_default && key < slice.firstTransfer && EntersTransfer(seg))
			{
				slice.firstTransfer = key;
				slice.firstTransferSeg = seg;
			}
			slice.area = key >= w.switchKey ? w.switchTo : w.initial;
		}
		back = gl_FakeFlat(seg->backsector, &bs, slice.area, true);
		if (LineCloses(seg->sidedef, front, back)) slice.clip.SafeAddClipRange(startAngle, endAngle);
	}
	// [rc4l] A two-sided line inside one sector with no middle texture shows nothing, and GL leaves it unmapped.
	if (seg->backsector != NULL && !polyobj && front->sectornum == seg->backsector->sectornum && !HasTexture(seg->sidedef, side_t::mid)) return;
	// [rc4l] The automap reveals what the player has seen.
	SetFlag(seg->linedef->flags, ML_MAPPED, w.direct, slice.mapLines);
	if (!g_deriving) return;
	if (!polyobj) NoteLineSeg(int(seg->linedef - lines), key, w.direct);
	if (!SkyWallsPossible(seg, front)) return;
	DeriveCandidate c = { key, seg, (int)slice.events.size(), 0 };
	DeriveSkyWalls(seg, front, back, sub, slice.events);
	c.count = (int)slice.events.size() - c.first;
	if (c.count == 0) return;
	slice.derive.push_back(c);
	g_rankDerived.Store(rank, g_walkStamp);
}

// [rc4l] UnclipSubsector: a subsector a stacked view's portal covers was seen from the other side, so the clipper opens over its segs.
void UnclipSubsector(ClipWalk &w, subsector_t *sub)
{
	seg_t *seg = sub->firstline;
	for (int count = sub->numlines; count--; seg++)
	{
		const unsigned startAngle = ClipAngle(w, seg->v2);
		const unsigned endAngle = ClipAngle(w, seg->v1);
		if (startAngle - endAngle >= 0x80000000u) w.slice->clip.SafeRemoveClipRange(startAngle, endAngle);
	}
}

void ClipPolyNode(ClipWalk &w, void *node, subsector_t *sub, sector_t *front, int rank, const std::vector<unsigned short> &pos, bool &drawn)
{
	while (!((size_t)node & 1))
	{
		node_t *bsp = (node_t *)node;
		int side = R_PointOnSide(viewx, viewy, bsp);
		ClipPolyNode(w, bsp->children[side], sub, front, rank, pos, drawn);
		side ^= 1;
		if (!BoxVisible(w.slice->clip, bsp->bbox[side])) return;
		node = bsp->children[side];
	}
	const subsector_t *leaf = (subsector_t *)((BYTE *)node - 1);
	seg_t *line = leaf->firstline;
	for (int count = leaf->numlines; count--; line++)
		if (line->linedef) ClipSeg(w, line, true, sub, front, rank, pos[line - &sub->BSP->Segs[0]], drawn);
}

// [rc4l] The flats pass's part that needs no order: whether GL processes the subsector (SSRF_PROCESSED, read by ProcessSectorStacks), and whether its ordered part has anything to feed the stacked views.
bool FlatsPassFirst(const ClipWalk &w, subsector_t *sub, sector_t *sector, sector_t *front)
{
	if (sub->numlines <= 2 && !(sub->hacked & 1)) return false;
	if (sector->heightsec && !(sector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC) && w.slice->area == area_default) return false;
	g_subProcessed.Store(sub - subsectors, 1);
	// [rc4l] One slice's view area can change as it goes, so walk order decides which subsector processes a render sector first.
	if (w.direct) return true;
	sector_t fake;
	const sector_t *flat = sector != sub->render_sector ? gl_FakeFlat(sub->render_sector, &fake, w.slice->area, false) : front;
	return flat->portals[sector_t::ceiling] != NULL || flat->portals[sector_t::floor] != NULL;
}

void ClipSubsector(ClipWalk &w, subsector_t *sub, int rank)
{
	sector_t *sector = sub->sector;
	if (!sector) return;
	if (!(currentmapsection[sub->mapsection >> 3] & (1 << (sub->mapsection & 7)))) return;
	const int si = int(sub - subsectors);
	if (g_spec.covered != NULL && g_spec.covered[si]) UnclipSubsector(w, sub);
	if (g_spec.mirror != NULL && SubsectorBehindMirror(sub)) return;
	Slice &slice = *w.slice;
	if (!w.direct) slice.area = rank > KeyRank(w.switchKey) ? w.switchTo : w.initial;
	g_rankSub.Store(rank, si);
	g_rankWalked.Store(rank, g_walkStamp);
	g_subWalked.Store(si, g_walkStamp);
	g_subAreaIn.Store(si, (unsigned char)slice.area);
	if (w.direct) slice.ranks.push_back(rank);
	if (g_subSeen.Load(si) != g_seenFrame)
	{
		g_subSeen.Store(si, g_seenFrame);
		SeeSector(sector);
		SeeSector(sub->render_sector);
		int fills = 0;
		const zx::levelmesh::FakePlane *fill = zx::levelmesh::FakePlanesFor(si, fills);
		for (int k = 0; k < fills; k++) SeeSector(&sectors[fill[k].model]);
	}
	if (w.direct && g_spec.slot >= 0) zx::levelmesh::NoteSlotVisible(g_spec.slot, si);
	SetFlag(sector->MoreFlags, SECF_DRAWN, w.direct, slice.drawnSectors);
	unsigned char flags = 0;
	if (g_spec.particles != NULL && si < (int)ParticlesInSubsec.Size() && ParticlesInSubsec[si] != NO_PARTICLE) flags |= kSubParticles;
	sector_t fake;
	sector_t *front = gl_FakeFlat(sector, &fake, slice.area, false);
	bool drawn = (sub->flags & SSECF_DRAWN) != 0;
	if (sub->polys != NULL)
	{
		const std::vector<unsigned short> &pos = g_polyPos[si];
		if (sub->BSP->Nodes.Size() == 0)
		{
			seg_t *line = sub->BSP->Subsectors[0].firstline;
			for (int count = sub->BSP->Subsectors[0].numlines; count--; line++)
				if (line->linedef) ClipSeg(w, line, true, sub, front, rank, pos[line - &sub->BSP->Segs[0]], drawn);
		}
		else ClipPolyNode(w, &sub->BSP->Nodes.Last(), sub, front, rank, pos, drawn);
	}
	else
	{
		seg_t *seg = sub->firstline;
		for (int i = 0; i < (int)sub->numlines; i++, seg++)
		{
			// [rc4l] A miniseg only marks its subsector seen, so once it is seen they are passed over.
			if (seg->linedef == NULL && drawn) continue;
			ClipSeg(w, seg, false, sub, front, rank, i, drawn);
		}
	}
	if (!w.direct) slice.area = w.switchKey != ~0ull && rank >= KeyRank(w.switchKey) ? w.switchTo : w.initial;
	g_subAreaOut.Store(si, (unsigned char)slice.area);
	if (g_spec.player && FlatsPassFirst(w, sub, sector, front)) flags |= kSubFlats;
	g_subFlags.Store(si, flags);
}

int LeavesUnder(void *node)
{
	return ((size_t)node & 1) ? 1 : g_leaves[(node_t *)node - nodes];
}

// [rc4l] Whether the walk goes into child `side` of `bsp`: GL's test of the node's box, which keeps whole units; a slice notes the segs under a child it passes over that end past the box and show in it, whose subsectors one walk reaches when another slice does.
bool EnterBox(ClipWalk &w, node_t *bsp, int side)
{
	const int b = int(bsp - nodes) * 2 + side;
	if (BoxVisible(w.slice->clip, bsp->bbox[side]))
	{
		if (!w.direct && (g_boxWatch[b] || g_boxForced[b])) g_boxEntered.Store(b, g_walkStamp);
		return true;
	}
	if (g_spec.nodeCovered != NULL && g_spec.nodeCovered[bsp - nodes]) return true;
	if (w.direct) return false;
	if (g_boxForced[b])
	{
		const LeakBox forced = { b, -1, true, false };
		w.slice->leaks.push_back(forced);
		return true;
	}
	for (int i = g_leakStart[b]; i < g_leakStart[b + 1]; i++)
	{
		seg_t *seg = segs + g_leakSegs[i];
		const unsigned startAngle = ClipAngle(w, seg->v2), endAngle = ClipAngle(w, seg->v1);
		if (startAngle - endAngle < 0x80000000u || !w.slice->clip.SafeCheckRange(startAngle, endAngle)) continue;
		const LeakBox passed = { b, int(seg->Subsector - subsectors), false, false };
		w.slice->leaks.push_back(passed);
	}
	return false;
}

void ClipNode(ClipWalk &w, void *node, int rank)
{
	while (!((size_t)node & 1))
	{
		node_t *bsp = (node_t *)node;
		int side = R_PointOnSide(viewx, viewy, bsp);
		ClipNode(w, bsp->children[side], rank);
		rank += LeavesUnder(bsp->children[side]);
		side ^= 1;
		if (!EnterBox(w, bsp, side)) return;
		node = bsp->children[side];
	}
	ClipSubsector(w, (subsector_t *)((BYTE *)node - 1), rank);
}

// [rc4l] One slice's share of the walk, on lane `lane`.
void ClipInto(Slice &slice, int lane, bool direct, unsigned long long switchKey, area_t switchTo)
{
	ClipWalk w = { &slice, &g_laneAngles[lane][0], direct, switchKey, slice.area, switchTo };
	if (numnodes == 0) ClipSubsector(w, subsectors, 0);
	else ClipNode(w, nodes + numnodes - 1, 0);
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
	if (flat->portals[sector_t::ceiling] != NULL) PortalFor(flat->portals[sector_t::ceiling], NULL).subs.push_back(sub);
	if (flat->portals[sector_t::floor] != NULL) PortalFor(flat->portals[sector_t::floor], NULL).subs.push_back(sub);
}

// [rc4l] A slice's candidate and the slice it is in.
struct CandidateRef { int slice, index; };
std::vector<CandidateRef> g_rankCandidates;

// [rc4l] The candidates every slice found at `rank` that build anything, in walk order, taken off the slices' cursors: a polyobject's every seg, and each line's first seen seg, once however many slices saw it.
void TakeRankCandidates(int rank, int count)
{
	g_rankCandidates.clear();
	if (g_rankDerived.Load(rank) != g_walkStamp) return;
	for (int k = 0; k < count; k++)
	{
		Slice &sl = g_slices[k];
		for (; sl.cursor < sl.derive.size() && KeyRank(sl.derive[sl.cursor].key) == rank; sl.cursor++)
		{
			const DeriveCandidate &c = sl.derive[sl.cursor];
			if (!(c.seg->sidedef->Flags & WALLF_POLYOBJ) && g_lineFirst.Load(c.seg->linedef - lines) != c.key) continue;
			// [rc4l] Slices hand in rising key order, so an insertion keeps the few here sorted and meets a repeat beside its twin.
			const CandidateRef ref = { k, (int)sl.cursor };
			size_t at = g_rankCandidates.size();
			while (at > 0 && g_slices[g_rankCandidates[at - 1].slice].derive[g_rankCandidates[at - 1].index].key > c.key) at--;
			if (at > 0 && g_slices[g_rankCandidates[at - 1].slice].derive[g_rankCandidates[at - 1].index].key == c.key) continue;
			g_rankCandidates.insert(g_rankCandidates.begin() + at, ref);
		}
	}
}

// [rc4l] What only walk order decides, for a subsector reached at `rank` by `count` slices: the order sectors were first reached in, the particles, the sky and stacked walls of the lines first seen here, and the flats pass that feeds the stacked views.
void ReplayRank(int rank, int count)
{
	const int si = g_rankSub.Load(rank);
	const int sec = g_subSector[si];
	g_walkSubs++;
	if (g_sectorWalked[sec] != g_walkStamp)
	{
		g_sectorWalked[sec] = g_walkStamp;
		g_sectorOrder.push_back(&sectors[sec]);
	}
	const unsigned char flags = g_subFlags.Load(si);
	subsector_t *sub = subsectors + si;
	sector_t fake;
	if (flags)
	{
		in_area = (area_t)g_subAreaIn.Load(si);
		g_sector = gl_FakeFlat(sub->sector, &fake, false);
		g_sub = sub;
	}
	if (flags & kSubParticles) g_spec.particles(sub, g_sector);
	TakeRankCandidates(rank, count);
	for (size_t i = 0; i < g_rankCandidates.size(); i++)
	{
		const Slice &sl = g_slices[g_rankCandidates[i].slice];
		const DeriveCandidate &c = sl.derive[g_rankCandidates[i].index];
		for (int e = 0; e < c.count; e++) ApplySkyEvent(sl.events[c.first + e]);
	}
	if (!(flags & kSubFlats)) return;
	in_area = (area_t)g_subAreaOut.Load(si);
	FlatsPass(sub, sub->sector);
}

// [rc4l] Ids (subsectors, then sidedefs) a chunk of the publish or seen-list scans covers; a multiple of the ids one word of walk bits holds.
const int kScanChunk = 4096;
std::vector<std::vector<int> > g_chunkIds;

// [rc4l] The ids [0, aCount) whose stamp in `a` is `stamp` and then aCount on those of `b` (bCount of them), scanned in chunks on the job pool into `out` in id order; with a slot, each chunk marks its ids' walk bits as it goes.
void ScanStamped(const zx::jobs::SharedArray<unsigned> *a, int aCount, const zx::jobs::SharedArray<unsigned> *b, int bCount, unsigned stamp, int slot,
	std::vector<int> &out)
{
	const int ids = aCount + bCount;
	const int chunks = (ids + kScanChunk - 1) / kScanChunk;
	if ((int)g_chunkIds.size() < chunks) g_chunkIds.resize(chunks);
	struct Scan
	{
		const zx::jobs::SharedArray<unsigned> *a, *b;
		int aCount, ids, slot;
		unsigned stamp;
		void operator()(int chunk, int) const
		{
			std::vector<int> &found = g_chunkIds[chunk];
			found.clear();
			const int end = (chunk + 1) * kScanChunk < ids ? (chunk + 1) * kScanChunk : ids;
			for (int id = chunk * kScanChunk; id < end; id++)
				if ((id < aCount ? a->Load(id) : b->Load(id - aCount)) == stamp) found.push_back(id);
			if (slot >= 0 && !found.empty()) zx::levelmesh::MarkSlotBits(slot, &found[0], (int)found.size());
		}
	} scan = { a, b, aCount, ids, slot, stamp };
	zx::jobs::Pool().ParallelFor(chunks, scan);
	out.clear();
	for (int c = 0; c < chunks; c++) out.insert(out.end(), g_chunkIds[c].begin(), g_chunkIds[c].end());
}

// [rc4l] The level's sectors with a height transfer, found once a level, whose transfer may still be switched off.
std::vector<sector_t *> g_transferSectors;

// [rc4l] Whether a line the walk crosses could change its view area, which only a height transfer in force can do.
bool AnyTransferInForce()
{
	for (size_t i = 0; i < g_transferSectors.size(); i++) if (TransfersHeight(g_transferSectors[i])) return true;
	return false;
}

// [rc4l] A subsector's polyobject BSP, built where it is missing or stale, as AddPolyobjs builds it before walking it.
void EnsurePolyBSP(subsector_t *sub)
{
	if (sub->BSP != NULL && !sub->BSP->bDirty) return;
	sub->BuildPolyBSP();
	for (unsigned i = 0; i < sub->BSP->Segs.Size(); i++)
	{
		sub->BSP->Segs[i].Subsector = sub;
		sub->BSP->Segs[i].PartnerSeg = NULL;
	}
}

// [rc4l] Number a polyobject BSP's segs in the order it is walked front to back from the eye.
void NumberPolySegs(void *node, subsector_t *sub, std::vector<unsigned short> &pos, int &next)
{
	while (!((size_t)node & 1))
	{
		node_t *bsp = (node_t *)node;
		const int side = R_PointOnSide(viewx, viewy, bsp);
		NumberPolySegs(bsp->children[side], sub, pos, next);
		node = bsp->children[side ^ 1];
	}
	const subsector_t *leaf = (subsector_t *)((BYTE *)node - 1);
	for (DWORD i = 0; i < leaf->numlines; i++)
		if (leaf->firstline[i].linedef) pos[leaf->firstline + i - &sub->BSP->Segs[0]] = (unsigned short)next++;
}

// [rc4l] Every polyobject subsector's BSP built and its segs numbered from this walk's eye, before any slice enters one.
void PreparePolyobjs()
{
	g_polyPos.clear();
	for (int i = 0; i < po_NumPolyobjs; i++)
		for (FPolyNode *pn = polyobjs[i].subsectorlinks; pn != NULL; pn = pn->snext)
		{
			subsector_t *sub = pn->subsector;
			EnsurePolyBSP(sub);
			std::vector<unsigned short> &pos = g_polyPos[int(sub - subsectors)];
			if (!pos.empty()) continue;
			pos.assign(sub->BSP->Segs.Size() + 1, 0);
			int next = 0;
			if (sub->BSP->Nodes.Size() == 0) NumberPolySegs((BYTE *)&sub->BSP->Subsectors[0] + 1, sub, pos, next);
			else NumberPolySegs(&sub->BSP->Nodes.Last(), sub, pos, next);
		}
}

// [rc4l] The per-level arrays sized and the BSP's coverage nodes, leaf counts and subsector sectors worked out, once a level.
void EnsureLevelArrays()
{
	const int now[6] = { numsubsectors, numsides, numlines, numsectors, numnodes, numvertexes };
	if (!memcmp(now, g_arraysFor, sizeof(now)) && (int)g_subSector.size() == numsubsectors) return;
	memcpy(g_arraysFor, now, sizeof(now));
	g_coverageNodes.resize(numnodes);
	for (int i = 0; i < numnodes; i++)
		for (int c = 0; c < 2; c++)
		{
			void *child = nodes[i].children[c];
			g_coverageNodes[i].child[c] = ((size_t)child & 1)
				? ~int((subsector_t *)((BYTE *)child - 1) - subsectors) : int((node_t *)child - nodes);
		}
	ComputeLeafCounts(numnodes ? &g_coverageNodes[0] : NULL, numnodes, g_leaves);
	g_nodeUp.assign(numnodes, -1);
	g_subUp.assign(numsubsectors, -1);
	for (int i = 0; i < numnodes; i++)
		for (int c = 0; c < 2; c++)
		{
			const int child = g_coverageNodes[i].child[c];
			if (child >= 0) g_nodeUp[child] = i * 2 + c;
			else g_subUp[~child] = i * 2 + c;
		}
	std::vector<long long> boxes(numnodes * 8), points(numsegs * 4);
	for (int i = 0; i < numnodes; i++)
		for (int c = 0; c < 2; c++)
			for (int e = 0; e < 4; e++) boxes[(i * 2 + c) * 4 + e] = nodes[i].bbox[c][e].Raw();
	for (int i = 0; i < numsegs; i++)
	{
		const long long p[4] = { segs[i].v1->x.Raw(), segs[i].v1->y.Raw(), segs[i].v2->x.Raw(), segs[i].v2->y.Raw() };
		memcpy(&points[i * 4], p, sizeof(p));
	}
	std::vector<int> subFirst(numsubsectors), subCount(numsubsectors);
	for (int i = 0; i < numsubsectors; i++)
	{
		subFirst[i] = int(subsectors[i].firstline - segs);
		subCount[i] = int(subsectors[i].numlines);
	}
	ComputeLeakySegs(numnodes ? &g_nodeUp[0] : NULL, numsubsectors ? &g_subUp[0] : NULL, numsubsectors, numsubsectors ? &subFirst[0] : NULL,
		numsubsectors ? &subCount[0] : NULL, numsegs ? &points[0] : NULL, numnodes ? &boxes[0] : NULL, numnodes * 2, g_leakStart, g_leakSegs);
	g_boxWatch.assign(numnodes * 2, 0);
	for (int b = 0; b < numnodes * 2; b++) g_boxWatch[b] = g_leakStart[b + 1] > g_leakStart[b];
	g_boxForced.assign(numnodes * 2, 0);
	g_forcedNow.clear();
	g_boxEntered.Resize(numnodes * 2, 0);
	g_lastTransfer = NULL;
	g_subSector.assign(numsubsectors, 0);
	for (int i = 0; i < numsubsectors; i++) g_subSector[i] = subsectors[i].sector ? subsectors[i].sector->sectornum : 0;
	g_transferSectors.clear();
	for (int i = 0; i < numsectors; i++) if (sectors[i].heightsec != NULL) g_transferSectors.push_back(&sectors[i]);
	g_rankWalked.Resize(numsubsectors, 0);
	g_rankDerived.Resize(numsubsectors, 0);
	g_rankSub.Resize(numsubsectors, 0);
	g_subWalked.Resize(numsubsectors, 0);
	g_subFlags.Resize(numsubsectors, 0);
	g_subAreaIn.Resize(numsubsectors, 0);
	g_subAreaOut.Resize(numsubsectors, 0);
	g_subProcessed.Resize(numsubsectors, 0);
	g_sideWalked.Resize(numsides, 0);
	g_lineFirst.Resize(numlines, 0);
	g_sectorWalked.assign(numsectors, 0);
	g_subSeen.Resize(numsubsectors, 0);
	g_sideSeen.Resize(numsides, 0);
	g_sectorSeen.Resize(numsectors, 0);
	const VertAngle unseen = { 0, 0 };
	for (size_t l = 0; l < g_laneAngles.size(); l++) g_laneAngles[l].assign(numvertexes + 1, unseen);
}

// [rc4l] The next walk's stamp; at a wrap of the stamp or its key epoch every mark goes, so an old one cannot read as this walk's.
void NextWalkStamp()
{
	if (++g_walkStamp == 0 || (g_walkStamp & 0xffffff) == 0)
	{
		g_rankWalked.Fill(0);
		g_rankDerived.Fill(0);
		g_subWalked.Fill(0);
		g_sideWalked.Fill(0);
		g_lineFirst.Fill(0);
		g_boxEntered.Fill(0);
		g_sectorWalked.assign(g_sectorWalked.size(), 0);
		const VertAngle unseen = { 0, 0 };
		for (size_t l = 0; l < g_laneAngles.size(); l++) g_laneAngles[l].assign(g_laneAngles[l].size(), unseen);
		g_walkStamp++;
	}
}

// [rc4l] The view area a sliced walk switches to, and the walk key it switches at (none: ~0).
unsigned long long g_switchKey = ~0ull;
area_t g_switchTo = area_default;

// [rc4l] Set the first `count` slices up from the seed clipper g_clip, split at `bounds` when there are several, and clip them, on the job pool when there are several, their view area switching at g_switchKey.
void ClipSlices(int count, const unsigned *bounds)
{
	if ((int)g_slices.size() < count) g_slices.resize(count);
	const int lanes = count > 1 ? zx::jobs::Pool().Lanes() : 1;
	const VertAngle unseen = { 0, 0 };
	while ((int)g_laneAngles.size() < lanes) g_laneAngles.push_back(std::vector<VertAngle>(numvertexes + 1, unseen));
	for (int k = 0; k < count; k++)
	{
		Slice &s = g_slices[k];
		s.clip = g_clip;
		if (count > 1) ComputeSliceSeed(s.clip, bounds[k], bounds[k + 1]);
		s.area = in_area;
		s.ranks.clear();
		s.derive.clear();
		s.events.clear();
		s.tried = s.facing = s.open = 0;
		s.firstTransfer = ~0ull;
		s.firstTransferSeg = NULL;
		s.mapLines.clear();
		s.drawnSubs.clear();
		s.drawnSectors.clear();
		s.leaks.clear();
		s.decalSides.clear();
	}
	if (count == 1) { ClipInto(g_slices[0], 0, true, ~0ull, in_area); return; }
	struct Run { void operator()(int index, int lane) const { ClipInto(g_slices[index], lane, false, g_switchKey, g_switchTo); } } run;
	zx::jobs::Pool().ParallelFor(count, run);
}

// [rc4l] Every rank `count` slices reached, in walk order, to `visit`.
template <class F> void ForEachRank(int count, F visit)
{
	if (count == 1)
		for (size_t i = 0; i < g_slices[0].ranks.size(); i++) visit(g_slices[0].ranks[i]);
	else
		for (int r = 0; r < numsubsectors; r++) if (g_rankWalked.Load(r) == g_walkStamp) visit(r);
}

// [rc4l] A walk's outcome flattened for comparison: each subsector reached in order with its particle flag (one slice runs every flats pass in order, so that flag differs by design) and view areas, the sidedefs seen, and the sky events in order.
void SignWalk(int count, std::vector<long long> &s)
{
	s.clear();
	for (int k = 0; k < count; k++) g_slices[k].cursor = 0;
	ForEachRank(count, [&s, count](int r)
	{
		const int si = g_rankSub.Load(r);
		s.push_back(r);
		s.push_back(si);
		s.push_back(g_subFlags.Load(si) & kSubParticles);
		s.push_back(g_subAreaIn.Load(si));
		s.push_back(g_subAreaOut.Load(si));
		TakeRankCandidates(r, count);
		for (size_t i = 0; i < g_rankCandidates.size(); i++)
		{
			const Slice &sl = g_slices[g_rankCandidates[i].slice];
			const DeriveCandidate &c = sl.derive[g_rankCandidates[i].index];
			s.push_back((long long)(c.key & ~kKeyEpochMask));
			for (int e = 0; e < c.count; e++)
			{
				const SkyEvent &ev = sl.events[c.first + e];
				long long xy;
				memcpy(&xy, &ev.x1, sizeof(xy));
				s.push_back(ev.kind);
				s.push_back(xy);
				s.push_back((long long)(size_t)ev.portal ^ (long long)(size_t)ev.skybox ^ (long long)(size_t)ev.mirror);
				s.push_back(ev.light);
			}
		}
	});
	s.push_back(-1);
	for (int side = 0; side < numsides; side++) if (g_sideWalked.Load(side) == g_walkStamp) s.push_back(side);
}

std::vector<long long> g_signOne, g_signMany;

// [rc4l] A subsector's rank from this eye: the leaves of every front child it lies behind on the way up.
int RankOf(int si)
{
	int rank = 0;
	for (int up = g_subUp[si]; up >= 0; up = g_nodeUp[up / 2])
	{
		node_t *bsp = nodes + up / 2;
		const int front = R_PointOnSide(viewx, viewy, bsp);
		if ((up & 1) != front) rank += LeavesUnder(bsp->children[front]);
	}
	return rank;
}

// [rc4l] Whether a remembered seg can still be where the view area switches: a seg of the level's own into a height transfer in force.
bool TransferStillHolds(const seg_t *seg)
{
	return seg != NULL && seg >= segs && seg < segs + numsegs && seg->sidedef != NULL && !(seg->sidedef->Flags & WALLF_POLYOBJ) &&
		seg->backsector != NULL && seg->frontsector != seg->backsector && seg->Subsector != NULL && seg->Subsector->polys == NULL &&
		EntersTransfer(seg);
}

// [rc4l] Force the node children `boxes` open for the next clip, and only them.
void ForceBoxes(const std::vector<int> &boxes)
{
	for (size_t i = 0; i < g_forcedNow.size(); i++) g_boxForced[g_forcedNow[i]] = 0;
	g_forcedNow = boxes;
	for (size_t i = 0; i < g_forcedNow.size(); i++) g_boxForced[g_forcedNow[i]] = 1;
}

std::vector<LeakBox> g_leakBoxes;
std::vector<int> g_nextForced;

// [rc4l] Clip the slices with the view area switching at `seg` (none: NULL), at its key from this walk's eye, with the node children one walk enters forced open, guessed to be the last walk's, and again with them worked out until the slices match one walk, which takes at most three clips; false when they never did.
bool ClipSwitchingAt(int count, const unsigned *bounds, seg_t *seg)
{
	for (int attempt = 0; attempt < 3; attempt++)
	{
		NextWalkStamp();
		g_switchKey = ~0ull;
		g_switchTo = in_area;
		if (seg != NULL)
		{
			subsector_t *sub = seg->Subsector;
			const int si = int(sub - subsectors);
			const int pos = sub->polys != NULL ? g_polyPos[si][seg - &sub->BSP->Segs[0]] : int(seg - sub->firstline);
			g_switchKey = WalkKey(RankOf(si), pos);
			g_switchTo = AreaAcross(seg);
		}
		ClipSlices(count, bounds);
		g_leakBoxes.clear();
		for (int k = 0; k < count; k++)
			for (size_t i = 0; i < g_slices[k].leaks.size(); i++)
			{
				LeakBox l = g_slices[k].leaks[i];
				l.entered = l.forced ? g_boxEntered.Load(l.box) == g_walkStamp : g_subWalked.Load(l.sub) == g_walkStamp;
				g_leakBoxes.push_back(l);
			}
		if (ComputeLeakForces(g_leakBoxes, numnodes ? &g_nodeUp[0] : NULL, &g_subUp[0], g_nextForced)) return true;
		ForceBoxes(g_nextForced);
		g_leakReruns++;
	}
	return false;
}

// [rc4l] Clip in `count` slices, or in one when they never matched one walk; the slice count it ended with.
int ClipOrOne(int count, const unsigned *bounds)
{
	if (ClipSwitchingAt(count, bounds, NULL)) return count;
	ClipSwitchingAt(1, NULL, NULL);
	return 1;
}

// [rc4l] The first line into a height transfer the slices saw, by walk key.
seg_t *FirstTransfer(int count, unsigned long long &key)
{
	key = ~0ull;
	seg_t *seg = NULL;
	for (int k = 0; k < count; k++)
		if (g_slices[k].firstTransfer < key) { key = g_slices[k].firstTransfer; seg = g_slices[k].firstTransferSeg; }
	return seg;
}

// [rc4l] Slices on a level with a height transfer, the eye in the open: the first line into one switches the view area, so guess it is last walk's, clip, and clip again from what the slices saw until the guess holds, which takes at most three; the slice count it ended with, one if the guesses never held.
int ClipGuessingTransfer(int count, const unsigned *bounds)
{
	seg_t *guess = TransferStillHolds(g_lastTransfer) ? g_lastTransfer : NULL;
	for (int attempt = 0; attempt < 3; attempt++)
	{
		if (!ClipSwitchingAt(count, bounds, guess)) break;
		unsigned long long key;
		seg_t *first = FirstTransfer(count, key);
		if (key == g_switchKey)
		{
			g_lastTransfer = TransferStillHolds(guess) ? guess : NULL;
			return count;
		}
		// [rc4l] A line before the guess is the true first, being clipped in the open as one walk would; with none, or after it, try no switch.
		guess = key < g_switchKey ? first : NULL;
		g_transferReruns++;
	}
	g_lastTransfer = NULL;
	ClipSwitchingAt(1, NULL, NULL);
	return 1;
}

// [rc4l] One walk from the current eye, its clipper seeded in g_clip: clipped in `count` slices split at `bounds`, then what only walk order decides applied in that order.
void Walk(const WalkSpec &spec, int count = 1, const unsigned *bounds = NULL)
{
	g_spec = spec;
	g_deriving = spec.player || spec.skyList != zx::levelmesh::kSkyCaptureNone || spec.portals != NULL;
	g_sectorOrder.clear();
	g_viewx = viewx.Raw();
	g_viewy = viewy.Raw();
	EnsureLevelArrays();
	if (po_NumPolyobjs > 0) PreparePolyobjs();
	const bool check = count > 1 && fua_vis_threadcheck;
	if (check)
	{
		// [rc4l] The same walk in one slice first, which the slices must match; what it marks, it marks alike.
		NextWalkStamp();
		ClipSlices(1, NULL);
		SignWalk(1, g_signOne);
	}
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	const area_t start = in_area;
	if (count > 1 && start == area_default && AnyTransferInForce()) count = ClipGuessingTransfer(count, bounds);
	else count = ClipOrOne(count, bounds);
	clock.Unclock();
	g_clipMs += clock.TimeMS();
	if (check)
	{
		SignWalk(count, g_signMany);
		g_sliceCheck.checked++;
		if (g_signOne != g_signMany)
		{
			size_t at = 0;
			while (at < g_signOne.size() && at < g_signMany.size() && g_signOne[at] == g_signMany[at]) at++;
			if (zx::frame::ComputeOracleDiffer(g_sliceCheck, 6))
				zx::frame::ComputeOracleNote(g_sliceCheck, "  %d slices: one walk has %u entries, the slices %u, first apart at %u\n", count,
					unsigned(g_signOne.size()), unsigned(g_signMany.size()), unsigned(at));
		}
	}
	if (count > 1)
	{
		// [rc4l] What the final clip saw, marked now that no clip of this walk will be thrown away.
		for (int k = 0; k < count; k++)
		{
			const Slice &sl = g_slices[k];
			for (size_t i = 0; i < sl.mapLines.size(); i++) *sl.mapLines[i] |= ML_MAPPED;
			for (size_t i = 0; i < sl.drawnSubs.size(); i++) *sl.drawnSubs[i] |= SSECF_DRAWN;
			for (size_t i = 0; i < sl.drawnSectors.size(); i++) *sl.drawnSectors[i] = WORD(*sl.drawnSectors[i] | SECF_DRAWN);
		}
		if (spec.slot >= 0)
		{
			ScanStamped(&g_subWalked, numsubsectors, &g_sideWalked, numsides, g_walkStamp, spec.slot, g_published);
			if (!g_published.empty()) zx::levelmesh::NoteSlotMarked(spec.slot, &g_published[0], (int)g_published.size());
		}
	}
	// [rc4l] One slice keeps its own view area; slices end in the one they switched to.
	const area_t after = count == 1 ? g_slices[0].area : g_switchKey != ~0ull ? g_switchTo : start;
	for (int k = 0; k < count; k++) g_slices[k].cursor = 0;
	ForEachRank(count, [count](int r) { ReplayRank(r, count); });
	in_area = after;
	g_decalSides.clear();
	for (int k = 0; k < count; k++)
	{
		g_walkSegs += g_slices[k].tried;
		g_walkFacing += g_slices[k].facing;
		g_walkOpen += g_slices[k].open;
		g_decalSides.insert(g_decalSides.end(), g_slices[k].decalSides.begin(), g_slices[k].decalSides.end());
	}
	std::sort(g_decalSides.begin(), g_decalSides.end());
	g_decalSides.erase(std::unique(g_decalSides.begin(), g_decalSides.end()), g_decalSides.end());
}

// [rc4l] The last walk's decal sidedefs to `hooks`, when it wants them.
void HandDecals(const WalkHooks *hooks)
{
	if (hooks != NULL && hooks->decals != NULL && !g_decalSides.empty()) hooks->decals(&g_decalSides[0], (int)g_decalSides.size());
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
	if (sub->numlines > 2 && !g_subProcessed.Load(sub - subsectors)) return;
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
				if (!g_subProcessed.Load(sub - subsectors)) continue;
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

// [rc4l] The sprite view inside a derived portal view, standing in for GL's mCurrentPortal and mViewActor there: no actor of its own, and in a mirror the mirror's clip; the rest is the outer view's.
const zx::hwrender::SpriteViewProvider *g_outerView = NULL;
line_t *g_viewMirror = NULL;
AActor *PortalViewActor() { return NULL; }
void PortalViewVector(float &x, float &y) { x = zx::viewsetup::CurrentView().vectorX; y = zx::viewsetup::CurrentView().vectorY; }
int PortalSpriteIndex() { return g_outerView->NextSpriteIndex(); }
bool PortalRejects(fixed_t x, fixed_t y) { return g_viewMirror != NULL && P_PointOnLineSide(x, y, g_viewMirror) != 0; }
bool PortalMirrors() { return g_viewMirror != NULL; }
int PortalLightCount() { return g_outerView->DynamicLightCount(); }
void PortalViewAngles(float &yaw, float &pitch) { yaw = zx::viewsetup::CurrentView().yaw; pitch = zx::viewsetup::CurrentView().pitch; }
const zx::hwrender::SpriteViewProvider g_portalView =
{
	PortalViewActor, PortalViewVector, PortalSpriteIndex, PortalRejects, PortalMirrors, PortalLightCount, PortalViewAngles
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
	origin->flags |= MF_JUSTHIT;
	zx::viewsetup::SetViewArea(origin);
	ComputeSeedPortal(g_clip, seenX, seenY, &p.lines[0], int(p.lines.size() / 4), viewangle - sa, viewangle, zx::viewsetup::CurrentView().frustumAngle);
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
}

// [rc4l] GLSectorStackPortal::DrawContents's walk: from the eye moved by the portal's displacement, over the map sections its subsectors cover, the clipper opened only where its walls were seen from; then its actors to `sprite` from inside the view, and the skyboxes it looks into.
void WalkStack(const PortalView &p, const WalkHooks *hooks)
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
	EnsureLevelArrays();
	g_nodeCovered.assign(numnodes, 0);
	if (numnodes > 0 && numsubsectors > 0) ComputeNodeCoverage(&g_coverageNodes[0], numnodes - 1, &g_covered[0], &g_nodeCovered[0]);
	ComputeSeedPortal(g_clip, g_playerX, g_playerY, &p.lines[0], int(p.lines.size() / 4), 0, viewangle, zx::viewsetup::CurrentView().frustumAngle);

	const fixed_t sx = viewx, sy = viewy;
	viewx += origin->xDisplacement;
	viewy += origin->yDisplacement;
	const int list = p.slot >= 0 ? zx::levelmesh::kSkyCaptureStack + p.slot : zx::levelmesh::kSkyCaptureNone;
	if (list != zx::levelmesh::kSkyCaptureNone) zx::levelmesh::BeginViewSky(list);
	std::vector<PortalView> skyboxes;
	const WalkSpec spec = { zx::levelmesh::ComputeStackWalkSlot(p.slot), list, false, NULL, &skyboxes, numsubsectors > 0 ? &g_covered[0] : NULL,
		numnodes > 0 ? &g_nodeCovered[0] : NULL };
	Walk(spec);

	// [rc4l] The view's actors, as GL's walk hands them to ProcessSprite, and its decals, captured into the view's slot while its map sections still stand.
	if (hooks != NULL && p.slot >= 0)
	{
		const zx::levelmesh::SceneScope scene;
		const zx::levelmesh::StackWalk walk(origin);
		if (hooks->sprite != NULL) PortalSprites(hooks->sprite, NULL);
		HandDecals(hooks);
	}
	const long long ex = viewx.Raw(), ey = viewy.Raw();
	for (size_t i = skyboxes.size(); i-- > 0; )
		if (!skyboxes[i].lines.empty()) WalkSkybox(skyboxes[i], ex, ey);
	currentmapsection = sections;
	viewx = sx;
	viewy = sy;
}

// [rc4l] GLMirrorPortal::DrawContents's walk: from the eye reflected in the line, turned to match, the frustum and the mirror's own span blocked and nothing behind the mirror walked; then its actors to `sprite` inside the reflection, and the skyboxes it looks into.
void WalkMirror(const PortalView &p, const WalkHooks *hooks)
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
	const unsigned half = zx::viewsetup::CurrentView().frustumAngle;
	if (half < 0x80000000u) ComputeSeedFrustum(g_clip, viewangle, half);
	g_clip.SafeAddClipRange(ComputePseudoAngle(double((line->v2->x - viewx).Raw()), double((line->v2->y - viewy).Raw())),
		ComputePseudoAngle(double((line->v1->x - viewx).Raw()), double((line->v1->y - viewy).Raw())));
	std::vector<PortalView> skyboxes;
	WalkSpec spec = { -1, zx::levelmesh::kSkyCaptureNone, false, NULL, &skyboxes, NULL, NULL, line };
	Walk(spec);
	if (hooks != NULL)
	{
		// [rc4l] Billboarded to the reflected view, as GL's SetupView turns its view vector for the mirror.
		zx::viewsetup::TurnView(viewangle);
		{
			const zx::levelmesh::SceneScope scene;
			const zx::levelmesh::MirrorWalk walk(int(line - lines));
			if (hooks->sprite != NULL) PortalSprites(hooks->sprite, line);
			HandDecals(hooks);
		}
		zx::viewsetup::TurnView(sa);
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
	g_walkCheck.checked++;
	int onlyGL = 0, onlyOurs = 0;
	const int ids = numsubsectors + numsides;
	const long long words = glBits.Size();
	for (int id = 0; id < ids; id++)
	{
		const long long bit = (long long)id * zx::levelmesh::WalkSlots() + zx::levelmesh::kMainWalkSlot;
		if (bit / 32 >= words) break;
		const bool gl = (glBits[unsigned(bit / 32)] >> (bit % 32)) & 1;
		const bool ours = id < numsubsectors ? g_subWalked.Load(id) == g_walkStamp : g_sideWalked.Load(id - numsubsectors) == g_walkStamp;
		if (gl && !ours) onlyGL++;
		if (ours && !gl) onlyOurs++;
		if (gl != ours && zx::frame::ComputeOracleRoom(g_walkCheck, 8, 2000))
			zx::frame::ComputeOracleNote(g_walkCheck, "  %s %d: %s\n", id < numsubsectors ? "subsector" : "side",
				id < numsubsectors ? id : id - numsubsectors, gl ? "GL only" : "ours only");
	}
	if (onlyGL || onlyOurs) g_walkCheck.differed++;
	g_onlyGL += onlyGL;
	g_onlyOurs += onlyOurs;
}

// [rc4l] The walk's sky surfaces and holes against GL's for the same frame, bit for bit.
void CheckSky(zx::levelmesh::MainSkyState &gl, zx::levelmesh::MainSkyState &ours)
{
	g_skyCheck.checked++;
	const char *why = NULL;
	if (gl.quads.Size() != ours.quads.Size()) why = "sky quad count";
	else if (gl.quads.Size() && memcmp(&gl.quads[0], &ours.quads[0], gl.quads.Size() * sizeof(zx::levelmesh::MeshVertex))) why = "sky quad";
	else if (gl.holes.Size() != ours.holes.Size()) why = "sky hole count";
	else if (gl.holes.Size() && memcmp(&gl.holes[0], &ours.holes[0], gl.holes.Size() * sizeof(float))) why = "sky hole";
	else if (gl.viewpoint != ours.viewpoint) why = "skybox";
	if (!why) return;
	if (zx::frame::ComputeOracleDiffer(g_skyCheck, 6))
		zx::frame::ComputeOracleNote(g_skyCheck, "  %s: GL %u quads %u holes skybox %s, ours %u quads %u holes skybox %s\n", why, gl.quads.Size() / 6,
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
		const unsigned bit = unsigned(id * zx::levelmesh::WalkSlots() + slot);
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
		const unsigned bit = unsigned(id * zx::levelmesh::WalkSlots() + slot);
		const bool inA = bit / 32 < a.Size() && ((a[bit / 32] >> (bit % 32)) & 1);
		const bool inB = bit / 32 < b.Size() && ((b[bit / 32] >> (bit % 32)) & 1);
		if (inA != inB) differ++;
	}
	return differ;
}

} // namespace

void WalkView(bool publish, bool derive, const WalkHooks *hooks)
{
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	const WalkSpec spec = { publish ? zx::levelmesh::kMainWalkSlot : -1, derive ? zx::levelmesh::kSkyCaptureMain : zx::levelmesh::kSkyCaptureNone,
		derive, zx::viewsetup::CurrentView().viewActor, derive ? &g_playerPortals : NULL, NULL, NULL, NULL, hooks != NULL ? hooks->particles : NULL };
	if (publish) zx::levelmesh::BeginSlotVisibility(zx::levelmesh::kMainWalkSlot);
	if (derive)
	{
		// [rc4l] A new frame of walks; at the wrap every stamp goes, so an old one cannot read as this frame's.
		EnsureLevelArrays();
		if (++g_seenFrame == 0)
		{
			g_subSeen.Fill(0);
			g_sideSeen.Fill(0);
			g_sectorSeen.Fill(0);
			g_seenFrame = 1;
		}
		g_seenListsReady = false;
		g_skyFadeSeen = false;
		zx::levelmesh::BeginViewSky(zx::levelmesh::kSkyCaptureMain);
		zx::levelmesh::BeginPortalViews();
		g_playerPortals.clear();
		g_subProcessed.Fill(0);
		g_secProcessed.assign(numsectors, 0);
		g_planeStacks[0].clear();
		g_planeStacks[1].clear();
		g_playerX = viewx.Raw();
		g_playerY = viewy.Raw();
	}
	const unsigned frustum = zx::viewsetup::CurrentView().frustumAngle;
	g_clip.Clear();
	ComputeSeedFrustum(g_clip, viewangle, frustum);
	int count = fua_vis_slices < 0 ? zx::jobs::Pool().Lanes() : fua_vis_slices < 1 ? 1 : *fua_vis_slices;
	if (count > kMaxSlices) count = kMaxSlices;
	if (zx::jobs::Pool().Workers() == 0) count = 1;
	unsigned bounds[kMaxSlices + 1];
	if (count > 1)
	{
		if ((int)g_sliceEdges.size() != count + 1 || !fua_vis_balance)
		{
			g_sliceEdges.resize(count + 1);
			for (int k = 0; k <= count; k++) g_sliceEdges[k] = double(k) / count;
		}
		ComputeWalkSlices(viewangle, frustum, count, &g_sliceEdges[0], bounds);
	}
	Walk(spec, count, bounds);
	if (count > 1 && fua_vis_balance)
	{
		g_sliceWork.resize(count);
		g_sliceNext.resize(count + 1);
		for (int k = 0; k < count; k++) g_sliceWork[k] = g_slices[k].tried;
		ComputeBalancedSlices(&g_sliceEdges[0], &g_sliceWork[0], count, 0.25 / count, &g_sliceNext[0]);
		g_sliceEdges.swap(g_sliceNext);
	}
	HandDecals(hooks);
	if (derive)
	{
		ProcessSectorStacks();
		const zx::hwrender::SkyFog fog = zx::hwrender::ComputeSkyFog(skyfog, gl_fixedcolormap != CM_DEFAULT, g_skyFadeSeen ? g_skyFade : 0);
		zx::hwrender::SetSkyFog(fog.r, fog.g, fog.b, fog.a);
	}
	zx::levelmesh::SetMainVisibility(publish);
	clock.Unclock();
	if (!publish) return;
	g_walkSlices += count;
	g_timedWalks++;
	g_walkMs += clock.TimeMS();
	if (clock.TimeMS() > g_walkMaxMs) g_walkMaxMs = clock.TimeMS();
}

void WalkPortalViews(const WalkHooks *hooks)
{
	cycle_t clock;
	clock.Reset();
	clock.Clock();
	// [rc4l] Last made first, as GLPortal::EndFrame pops them; GL renders only a portal with lines.
	const std::vector<PortalView> portals(g_playerPortals);
	for (size_t i = portals.size(); i-- > 0; )
	{
		if (portals[i].lines.empty()) continue;
		if (portals[i].stack != NULL) WalkStack(portals[i], hooks);
		else if (portals[i].mirror != NULL)
		{
			zx::levelmesh::AddWalkedMirror(int(portals[i].mirror - lines));
			WalkMirror(portals[i], hooks);
		}
		else WalkSkybox(portals[i], g_playerX, g_playerY);
	}
	clock.Unclock();
	g_portalWalkMs += clock.TimeMS();
}

// [rc4l] The seen sectors and sidedefs as lists, scanned from their stamps once a frame.
static void MakeSeenLists()
{
	if (g_seenListsReady) return;
	g_seenListsReady = true;
	ScanStamped(&g_sectorSeen, int(g_sectorSeen.Size()), NULL, 0, g_seenFrame, -1, g_seenSectors);
	ScanStamped(&g_sideSeen, int(g_sideSeen.Size()), NULL, 0, g_seenFrame, -1, g_seenSides);
}

const int *SectorsWalkedThisFrame(int &count)
{
	MakeSeenLists();
	count = (int)g_seenSectors.size();
	return count ? &g_seenSectors[0] : NULL;
}

const int *SidesWalkedThisFrame(int &count)
{
	MakeSeenLists();
	count = (int)g_seenSides.size();
	return count ? &g_seenSides[0] : NULL;
}

void ForgetWalk()
{
	g_subSeen.Clear();
	g_sideSeen.Clear();
	g_sectorSeen.Clear();
	g_seenSides.clear();
	g_seenSectors.clear();
	g_seenListsReady = false;
	g_sectorOrder.clear();
	g_laneAngles.clear();
	g_slices.clear();
	g_leaves.clear();
	g_subSector.clear();
	g_transferSectors.clear();
	g_nodeUp.clear();
	g_subUp.clear();
	g_lastTransfer = NULL;
	g_rankWalked.Clear();
	g_rankDerived.Clear();
	g_rankSub.Clear();
	g_subWalked.Clear();
	g_subFlags.Clear();
	g_subAreaIn.Clear();
	g_subAreaOut.Clear();
	g_subProcessed.Clear();
	g_sideWalked.Clear();
	g_lineFirst.Clear();
	g_sectorWalked.clear();
	g_polyPos.clear();
	for (int k = 0; k < 6; k++) g_arraysFor[k] = -1;
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
	g_portalCheck.checked++;
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
		else if (SlotCount(gl.bits, zx::levelmesh::ComputeStackWalkSlot(i)) == 0 && gl.sky[i].Size() == 0) unwalked = true;
		else if (!SameVertices(gl.sky[i], ours.sky[i]))
			why.Format("stacked view %d: sky, GL %u quads, ours %u", i, gl.sky[i].Size() / 6, ours.sky[i].Size() / 6);
		else
		{
			const int differ = SlotDiffers(gl.bits, ours.bits, zx::levelmesh::ComputeStackWalkSlot(i));
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
	if (zx::frame::ComputeOracleDiffer(g_portalCheck, 6)) zx::frame::ComputeOracleNote(g_portalCheck, "  %s\n", why.GetChars());
}

}} // namespace zx::visibility

CCMD( fua_vis_report )
{
	const int walks = zx::visibility::g_timedWalks;
	Printf( "player walks: %d, mean %.3f ms (max %.3f), portal views %.3f ms a frame\n", walks,
		walks ? zx::visibility::g_walkMs / walks : 0.0, zx::visibility::g_walkMaxMs, walks ? zx::visibility::g_portalWalkMs / walks : 0.0 );
	if (walks) Printf( "  clipping %.3f ms a frame over every walk, the player's in %.1f slices, %d clipped again for a height transfer\n",
		zx::visibility::g_clipMs / walks, double(zx::visibility::g_walkSlices) / walks, zx::visibility::g_transferReruns );
	if (walks) Printf( "  %d clipped again for a node box one walk enters past what another slice saw\n", zx::visibility::g_leakReruns );
	zx::visibility::g_transferReruns = 0;
	zx::visibility::g_leakReruns = 0;
	zx::visibility::g_clipMs = 0;
	zx::visibility::g_walkSlices = 0;
	if (walks) Printf( "  a frame's walks reach %lld subsectors and %lld segs, %lld facing the eye, %lld past the clipper\n",
		zx::visibility::g_walkSubs / walks, zx::visibility::g_walkSegs / walks, zx::visibility::g_walkFacing / walks, zx::visibility::g_walkOpen / walks );
	zx::visibility::g_walkSubs = zx::visibility::g_walkSegs = zx::visibility::g_walkFacing = zx::visibility::g_walkOpen = 0;
	zx::visibility::g_timedWalks = 0;
	zx::visibility::g_walkMs = zx::visibility::g_walkMaxMs = zx::visibility::g_portalWalkMs = 0;
	const zx::frame::OracleTally &w = zx::visibility::g_walkCheck, &s = zx::visibility::g_skyCheck, &p = zx::visibility::g_portalCheck;
	Printf( "visibility: %d frames walked beside GL, %d differed (%d ids only GL reached, %d only ours)\n%s",
		w.checked, w.differed, zx::visibility::g_onlyGL, zx::visibility::g_onlyOurs, w.firsts.c_str( ) );
	Printf( "sky: %d frames derived beside GL, %d differed\n%s", s.checked, s.differed, s.firsts.c_str( ) );
	Printf( "portal views: %d frames derived beside GL (%d stacked views, %d mirrors, %d sheens), %d differed, %d with a view GL's occlusion query left unwalked\n%s",
		p.checked, zx::visibility::g_stackViewsSeen, zx::visibility::g_mirrorsSeen, zx::visibility::g_sheensSeen,
		p.differed, zx::visibility::g_portalUnwalked, p.firsts.c_str( ) );
	const zx::frame::OracleTally &t = zx::visibility::g_sliceCheck;
	if (t.checked) Printf( "slices: %d walks compared with one walk, %d differed\n%s", t.checked, t.differed, t.firsts.c_str( ) );
	zx::frame::ComputeOracleReset( zx::visibility::g_sliceCheck );
	zx::frame::ComputeOracleReset( zx::visibility::g_walkCheck );
	zx::frame::ComputeOracleReset( zx::visibility::g_skyCheck );
	zx::frame::ComputeOracleReset( zx::visibility::g_portalCheck );
	zx::visibility::g_portalUnwalked = 0;
	zx::visibility::g_stackViewsSeen = zx::visibility::g_mirrorsSeen = zx::visibility::g_sheensSeen = 0;
	zx::visibility::g_onlyGL = zx::visibility::g_onlyOurs = 0;
}
