// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/gamecopy.h"
#include "features/gamecopy/actors.h"
#include "features/gamecopy/players.h"
#include "features/gamecopy/xfloors.h"
#include "features/gamecopy/lights.h"
#include "features/gamecopy/level.h"
#include "features/gamecopy/particles.h"
#include "features/gamecopy/decals.h"
#include "features/gamecopy/seen.h"
#include "features/gamecopy/polyobjs.h"
#include "features/postprocess/postprocess.h"   // [Doruk-Aksoy] SetDrawnShaders
#include "features/models/models.h"   // [rc4l] gl_IsHUDModelFor
#include "features/viewsetup/computation/viewsetup_compute.h"   // [rc4l] kTorchPower, kLightAmpPower
#include "features/gamecopy/computation/recordlinks_compute.h"
#include "features/gamecopy/computation/filldue_compute.h"
#include "features/gamecopy/computation/serial_compute.h"
#include "features/gamecopy/computation/prescan_compute.h"
#include "features/gamecopy/computation/changelog_compute.h"
#include "features/handoff/effects.h"   // [rc4l] EffectsRun, Deferring
#include "features/gamecopy/actorlists.h"
#include "features/gamecopy/tintsubject.h"
#include "features/frame/computation/oracle_compute.h"
#include "features/handoff/seam.h"
#include "features/telemetry/telemetry.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomstat.h"
#include "network.h"
#include "features/net-session/netrole.h"
#include "features/net-client/netclient.h"   // [rc4l] the server packets a client applied, which change its world between tics
#include "d_player.h"
#include "doomtype.h"
#include "a_pickups.h"
#include "a_artifacts.h"
#include "g_shared/a_sharedglobal.h"
#include "r_utility.h"
#include "v_palette.h"
#include "features/jobs/jobs.h"   // [rc4l] the change scans split across the pool
#include "g_level.h"
#include "g_game.h"   // [rc4l] GAME_GetEndLevelDelay
#include "cl_demo.h"   // [rc4l] CLIENTDEMO_IsInFreeSpectateMode
#include "p_local.h"
#include "po_man.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_data/r_interpolate.h"
#include "r_sky.h"
#include "r_data/r_translate.h"
#include "textures/textures.h"

#include <algorithm>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

// [rc4l] Holds the render copy to the live game while tics run inline: before each frame's views, and after each view's interpolations.
CVAR(Bool, fua_copy_check, false, 0)
// [rc4l] Holds each fill that took the tic's end scan to the whole-map scan it spared, counting what the scan missed, for fuactl fixture prescancheck.
CVAR(Bool, fua_copy_prescan_check, false, 0)

namespace zx { namespace gamecopy {

unsigned int g_actorLists = 0;

namespace {

// [rc4l] What a change to a member means to the renderer's caches.
enum { kShape = 1, kPlacement = 2, kLook = 4 };

Blends g_blends;      // [rc4l] what frames blend, from the last fill
FillStamp g_filled;   // [rc4l] the tic and server packets the copy was last taken at, tic -1 for none this level
bool g_levelLoaded = false;   // [rc4l] whether a level is set up for the copy to read, from InitLevel until ForgetLevel
Blends g_gathering;   // [rc4l] what the interpolations add while a fill gathers
zx::frame::OracleTally g_before, g_after;

// [rc4l] Each member type compared by its value, never by the padding beside it.
bool Same(const zx::Fixed &a, const zx::Fixed &b) { return a.Raw() == b.Raw(); }
template <class T> typename std::enable_if<std::is_integral<T>::value || std::is_pointer<T>::value, bool>::type Same(T a, T b) { return a == b; }
bool Same(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }
bool Same(FTextureID a, FTextureID b) { return a == b; }
template <class T, class TT> bool Same(const TFlags<T, TT> &a, const TFlags<T, TT> &b) { return a.GetValue() == b.GetValue(); }
bool Same(const FRenderStyle &a, const FRenderStyle &b) { return a.AsDWORD == b.AsDWORD; }
bool Same(const pspdef_t &a, const pspdef_t &b)
{
	return a.state == b.state && a.tics == b.tics && Same(a.sx, b.sx) && Same(a.sy, b.sy) && a.sprite == b.sprite && a.frame == b.frame &&
		a.processPending == b.processPending && a.layer == b.layer && a.Flags == b.Flags && Same(a.alpha, b.alpha) && Same(a.RenderStyle, b.RenderStyle) &&
		Same(a.scalex, b.scalex) && Same(a.scaley, b.scaley) && Same(a.oldx, b.oldx) && Same(a.oldy, b.oldy) && a.bInterpolate == b.bInterpolate;
}
template <class T> bool Same(const std::vector<T> &a, const std::vector<T> &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++) if (!Same(a[i], b[i])) return false;
	return true;
}
bool Same(const FWeaponBobIn &a, const FWeaponBobIn &b)
{
	return a.spectating == b.spectating && a.weapon == b.weapon && a.bobStyle == b.bobStyle && Same(a.bobSpeed, b.bobSpeed) && Same(a.bobRangeX, b.bobRangeX) &&
		Same(a.bobRangeY, b.bobRangeY) && Same(a.stillBobRange, b.stillBobRange) && Same(a.stillBobSpeed, b.stillBobSpeed) && Same(a.viewSwaySpeed, b.viewSwaySpeed) &&
		Same(a.motionSwaySpeed, b.motionSwaySpeed) && Same(a.jumpSwaySpeed, b.jumpSwaySpeed) && a.swayStyle == b.swayStyle && a.viewPitchStyle == b.viewPitchStyle &&
		Same(a.viewPitchOffset, b.viewPitchOffset) && a.levelTime == b.levelTime && a.bobbing == b.bobbing && Same(a.bob, b.bob) && a.swayRunning == b.swayRunning &&
		Same(a.angleDelta, b.angleDelta) && Same(a.pitchDelta, b.pitchDelta) && Same(a.jumpVelz, b.jumpVelz) && Same(a.velz, b.velz) && Same(a.z, b.z) &&
		Same(a.prevZ, b.prevZ) && Same(a.pitch, b.pitch) && a.onground == b.onground && a.jumpTics == b.jumpTics && Same(a.crouchViewDelta, b.crouchViewDelta);
}
bool Same(const ScreenBlendLayers &a, const ScreenBlendLayers &b)
{
	if (a.applies != b.applies || !Same(a.cap, b.cap) || a.layers.size() != b.layers.size()) return false;
	for (size_t i = 0; i < a.layers.size(); i++)
		if (!Same(a.layers[i].r, b.layers[i].r) || !Same(a.layers[i].g, b.layers[i].g) || !Same(a.layers[i].b, b.layers[i].b) || !Same(a.layers[i].a, b.layers[i].a)) return false;
	return true;
}
bool Same(const FTransform &a, const FTransform &b)
{
	return Same(a.xoffs, b.xoffs) && Same(a.yoffs, b.yoffs) && Same(a.xscale, b.xscale) && Same(a.yscale, b.yscale) && Same(a.angle, b.angle) &&
		Same(a.base_angle, b.base_angle) && Same(a.base_yoffs, b.base_yoffs);
}
bool Same(const secplane_core_t &a, const secplane_core_t &b)
{
	return Same(a.a, b.a) && Same(a.b, b.b) && Same(a.c, b.c) && Same(a.d, b.d) && Same(a.ic, b.ic);
}
bool Same(const side_part_t &a, const side_part_t &b)
{
	return Same(a.xoffset, b.xoffset) && Same(a.yoffset, b.yoffset) && Same(a.xscale, b.xscale) && Same(a.yscale, b.yscale) && Same(a.texture, b.texture);
}
template <class T, size_t N> bool Same(const T (&a)[N], const T (&b)[N])
{
	for (size_t i = 0; i < N; i++) if (!Same(a[i], b[i])) return false;
	return true;
}

// [rc4l] A plane's texture moving is placement, anything else about it reshapes the walls round it.
int ChangePlane(const sector_splane_t (&rc)[2], const sector_splane_t (&live)[2])
{
	int change = 0;
	for (int i = 0; i < 2; i++)
	{
		if (!Same(rc[i].xform, live[i].xform)) change |= kPlacement;
		if (!Same(rc[i].Flags, live[i].Flags) || !Same(rc[i].Light, live[i].Light) || !Same(rc[i].alpha, live[i].alpha) ||
			!Same(rc[i].Texture, live[i].Texture) || !Same(rc[i].TexZ, live[i].TexZ)) change |= kShape;
	}
	return change;
}
template <class R, class L> int ChangeShape(const R &rc, const L &live) { return Same(rc, live) ? 0 : kShape; }
template <class R, class L> int ChangeLook(const R &rc, const L &live) { return Same(rc, live) ? 0 : (kLook | kShape); }

template <class R, class L> void Take(R &rc, const L &live) { rc = live; }
template <class T, size_t N> void Take(T (&rc)[N], const T (&live)[N]) { for (size_t i = 0; i < N; i++) Take(rc[i], live[i]); }

// [rc4l] Every listed member holds the type the live structure's does (a plane its core), so the list cannot name a member the copy lacks.
#define ZX_COPY_FIELD(kind, member, group) ZX_TYPECHECK_##kind(member)
#define ZX_TYPECHECK_sector(m) static_assert(std::is_convertible<const decltype(sector_t::m) &, const decltype(SectorState::m) &>::value || std::is_same<decltype(sector_t::m), decltype(SectorState::m)>::value, "sector " #m);
#define ZX_TYPECHECK_side(m) static_assert(std::is_same<decltype(side_t::m), decltype(SideState::m)>::value, "side " #m);
#define ZX_TYPECHECK_line(m) static_assert(std::is_same<decltype(line_t::m), decltype(LineState::m)>::value, "line " #m);
#define ZX_TYPECHECK_vertex(m) static_assert(std::is_same<decltype(vertex_t::m), decltype(VertexState::m)>::value, "vertex " #m);
#include "features/gamecopy/fields.h"
#undef ZX_COPY_FIELD
#undef ZX_TYPECHECK_sector
#undef ZX_TYPECHECK_side
#undef ZX_TYPECHECK_line
#undef ZX_TYPECHECK_vertex

// [rc4l] The list names every member of each state: the same members in the same order make a struct of the same size.
#define ZX_COPY_FIELD(kind, member, group) ZX_LISTED_##kind(member)
#define ZX_LISTED_sector(m) decltype(SectorState::m) m;
#define ZX_LISTED_side(m)
#define ZX_LISTED_line(m)
#define ZX_LISTED_vertex(m)
struct SectorListed {
#include "features/gamecopy/fields.h"
};
#undef ZX_LISTED_sector
#undef ZX_LISTED_side
#define ZX_LISTED_sector(m)
#define ZX_LISTED_side(m) decltype(SideState::m) m;
struct SideListed {
#include "features/gamecopy/fields.h"
};
#undef ZX_LISTED_side
#undef ZX_LISTED_line
#define ZX_LISTED_side(m)
#define ZX_LISTED_line(m) decltype(LineState::m) m;
struct LineListed {
#include "features/gamecopy/fields.h"
};
#undef ZX_LISTED_line
#undef ZX_LISTED_vertex
#define ZX_LISTED_line(m)
#define ZX_LISTED_vertex(m) decltype(VertexState::m) m;
struct VertexListed {
#include "features/gamecopy/fields.h"
};
#undef ZX_COPY_FIELD
#undef ZX_LISTED_sector
#undef ZX_LISTED_side
#undef ZX_LISTED_line
#undef ZX_LISTED_vertex
static_assert(sizeof(SectorListed) == sizeof(SectorState), "fields.h and SectorState list different members");
static_assert(sizeof(SideListed) == sizeof(SideState), "fields.h and SideState list different members");
static_assert(sizeof(LineListed) == sizeof(LineState), "fields.h and LineState list different members");
static_assert(sizeof(VertexListed) == sizeof(VertexState), "fields.h and VertexState list different members");

// [rc4l] How a live structure differs from a state it was taken in: its copy, or the scan's record of the copy as last taken.
int SectorChangeFrom(const SectorState &c, const sector_t &s)
{
	int change = 0;
#define ZX_COPY_FIELD(kind, member, group) ZX_CHANGE_##kind(member, group)
#define ZX_CHANGE_sector(m, g) change |= Change##g(c.m, s.m);
#define ZX_CHANGE_side(m, g)
#define ZX_CHANGE_line(m, g)
#define ZX_CHANGE_vertex(m, g)
#include "features/gamecopy/fields.h"
#undef ZX_CHANGE_sector
#undef ZX_CHANGE_side
#define ZX_CHANGE_sector(m, g)
#define ZX_CHANGE_side(m, g) change |= Change##g(c.m, d.m);
	return change;
}

int SideChangeFrom(const SideState &c, const side_t &d)
{
	int change = 0;
#include "features/gamecopy/fields.h"
	return change;
}
#undef ZX_CHANGE_side
#undef ZX_CHANGE_line
#define ZX_CHANGE_side(m, g)
#define ZX_CHANGE_line(m, g) change |= Change##g(c.m, l.m);

int LineChangeFrom(const LineState &c, const line_t &l)
{
	int change = 0;
#include "features/gamecopy/fields.h"
	return change;
}
#undef ZX_CHANGE_line
#undef ZX_CHANGE_vertex
#define ZX_CHANGE_line(m, g)
#define ZX_CHANGE_vertex(m, g) change |= Change##g(v.rc.m, v.m);

int VertexChange(const vertex_t &v)
{
	int change = 0;
#include "features/gamecopy/fields.h"
	return change;
}

int SectorChange(const sector_t &s) { return SectorChangeFrom(s.rc, s); }
int SideChange(const side_t &d) { return SideChangeFrom(d.rc, d); }
int LineChange(const line_t &l) { return LineChangeFrom(l.rc, l); }
#undef ZX_COPY_FIELD
#undef ZX_CHANGE_sector
#undef ZX_CHANGE_side
#undef ZX_CHANGE_line
#undef ZX_CHANGE_vertex

// [rc4l] The first member a structure's copy holds differently, for fua_copy_report.
#define ZX_COPY_FIELD(kind, member, group) ZX_DIFFER_##kind(member, group)
#define ZX_DIFFER_sector(m, g) if (Change##g(s.rc.m, s.m)) return #m;
#define ZX_DIFFER_side(m, g)
#define ZX_DIFFER_line(m, g)
#define ZX_DIFFER_vertex(m, g)
const char *SectorDiffers(const sector_t &s)
{
#include "features/gamecopy/fields.h"
	return NULL;
}
#undef ZX_DIFFER_sector
#undef ZX_DIFFER_side
#define ZX_DIFFER_sector(m, g)
#define ZX_DIFFER_side(m, g) if (Change##g(d.rc.m, d.m)) return #m;
const char *SideDiffers(const side_t &d)
{
#include "features/gamecopy/fields.h"
	return NULL;
}
#undef ZX_DIFFER_side
#undef ZX_DIFFER_line
#define ZX_DIFFER_side(m, g)
#define ZX_DIFFER_line(m, g) if (Change##g(l.rc.m, l.m)) return #m;
const char *LineDiffers(const line_t &l)
{
#include "features/gamecopy/fields.h"
	return NULL;
}
#undef ZX_COPY_FIELD
#undef ZX_DIFFER_sector
#undef ZX_DIFFER_side
#undef ZX_DIFFER_line
#undef ZX_DIFFER_vertex

#define ZX_COPY_FIELD(kind, member, group) ZX_TAKE_##kind(member)
#define ZX_TAKE_sector(m) Take(s.rc.m, s.m);
#define ZX_TAKE_side(m)
#define ZX_TAKE_line(m)
#define ZX_TAKE_vertex(m)
void TakeSector(sector_t &s)
{
#include "features/gamecopy/fields.h"
}
#undef ZX_TAKE_sector
#undef ZX_TAKE_side
#define ZX_TAKE_sector(m)
#define ZX_TAKE_side(m) Take(d.rc.m, d.m);
void TakeSide(side_t &d)
{
#include "features/gamecopy/fields.h"
}
#undef ZX_TAKE_side
#undef ZX_TAKE_line
#define ZX_TAKE_side(m)
#define ZX_TAKE_line(m) Take(l.rc.m, l.m);
void TakeLine(line_t &l)
{
#include "features/gamecopy/fields.h"
}
#undef ZX_TAKE_line
#undef ZX_TAKE_vertex
#define ZX_TAKE_line(m)
#define ZX_TAKE_vertex(m) Take(v.rc.m, v.m);
void TakeVertex(vertex_t &v)
{
#include "features/gamecopy/fields.h"
}
#undef ZX_COPY_FIELD
#undef ZX_TAKE_sector
#undef ZX_TAKE_side
#undef ZX_TAKE_line
#undef ZX_TAKE_vertex

// [rc4l] The vertices whose wall heights a sector's planes decide, its own and those of the sectors drawn with its planes, as SetAllVerticesDirty marks them live.
void MarkVertexHeights(sector_t &s)
{
	if (s.e == NULL) return;
	for (unsigned i = 0; i < s.e->vertices.Size(); i++) s.e->vertices[i]->rcDirty = true;
	for (unsigned k = 0; k < s.e->FakeFloor.Sectors.Size(); k++)
	{
		extsector_t *e = s.e->FakeFloor.Sectors[k]->e;
		if (e != NULL) for (unsigned i = 0; i < e->vertices.Size(); i++) e->vertices[i]->rcDirty = true;
	}
	for (unsigned k = 0; k < s.e->XFloor.attached.Size(); k++)
	{
		extsector_t *e = s.e->XFloor.attached[k]->e;
		if (e != NULL) for (unsigned i = 0; i < e->vertices.Size(); i++) e->vertices[i]->rcDirty = true;
	}
}

void Bump(sector_t &s, int change)
{
	if (change & kShape) { s.rcShape++; MarkVertexHeights(s); }
	if (change & kPlacement) s.rcPlacement++;
	if (change & kLook) s.rcLook++;
}

// [rc4l] Items each pool lane compares at a time.
static const int kScanChunk = 2048;

// [rc4l] Each of count items' change worked out across the job pool, since on Sunder MAP16 the sector and side scans alone were 2.4 ms of a tic's handoff; it only reads, so the copy is taken afterwards on this thread.
template <class Change> static void ScanChanges(int count, std::vector<int> &out, Change change)
{
	out.resize(count > 0 ? count : 0);
	auto scan = [&](int lo, int hi, int) { for (int i = lo; i < hi; i++) out[i] = change(i); };
	zx::jobs::Pool().ParallelChunks(count, kScanChunk, scan);
}

// [rc4l] The map as the copy last took it, which a scan beside the frames compares against, since the frames blend the copy itself.
std::vector<SectorState> g_sectorTaken;
std::vector<SideState> g_sideTaken;
std::vector<LineState> g_lineTaken;

// [Doruk-Aksoy] The sides changed since the last fill, as their setters noted them, which a tic's end compares instead of all of them.
ChangeLog g_sideLog;
int g_sideSlice = 0;   // [Doruk-Aksoy] where the next slice compared whole starts
// [Doruk-Aksoy] Sides compared whole at each tic's end besides those noted, so a write nobody noted is still taken within a few seconds and counted.
const int kSideSlice = 1024;

// [rc4l] Every item's state as the copy holds it now kept for the next scan, as a level starts.
void KeepAllTaken()
{
	g_sectorTaken.resize(numsectors);
	g_sideTaken.resize(numsides);
	g_lineTaken.resize(numlines);
	for (int i = 0; i < numsectors; i++) g_sectorTaken[i] = sectors[i].rc;
	for (int i = 0; i < numsides; i++) g_sideTaken[i] = sides[i].rc;
	for (int i = 0; i < numlines; i++) g_lineTaken[i] = lines[i].rc;
}

// [rc4l] What the last tic changed of the map, found as it ended, and when.
struct Prescan
{
	PrescanStamp stamp;
	std::vector<int> sectors, sides, lines;
};
Prescan g_prescan;
unsigned g_fills = 0;   // [rc4l] fills since the start, which a scan is stamped with
const std::vector<int> kNoneScanned;

// [rc4l] The indices of count items that changed: across the pool on the main thread, one by one on the game thread, which leaves the pool to the frames.
template <class Changed> static void ListChanges(int count, std::vector<int> &out, Changed changed)
{
	out.clear();
	if (zx::handoff::Deferring())
	{
		for (int i = 0; i < count; i++) if (changed(i)) out.push_back(i);
		return;
	}
	static std::vector<int> flags;
	ScanChanges(count, flags, [&](int i) { return changed(i) ? 1 : 0; });
	for (int i = 0; i < count; i++) if (flags[i]) out.push_back(i);
}

// [rc4l] Sector i taken if it changed, and the state its copy is in now kept for the next scan.
void FillSector(int i, int change)
{
	if (change != 0) { TakeSector(sectors[i]); Bump(sectors[i], change); }
	g_sectorTaken[i] = sectors[i].rc;
}

// [rc4l] The sectors that changed taken, of the candidates alone when the tic's end scanned the map and else of every one, the candidates' kept states refreshed either way since a blended copy can match the game untaken.
void FillSectors(const std::vector<int> &candidates, bool scanned)
{
	if (scanned)
	{
		for (int i : candidates) FillSector(i, SectorChange(sectors[i]));
		return;
	}
	static std::vector<int> changes;
	ScanChanges(numsectors, changes, [](int i) { return SectorChange(sectors[i]); });
	for (int i = 0; i < numsectors; i++) if (changes[i] != 0) FillSector(i, changes[i]);
	for (int i : candidates) g_sectorTaken[i] = sectors[i].rc;
}

// [rc4l] Side i taken if it changed, and its copy's state kept; true when taken.
bool FillSide(int i, int change)
{
	if (change != 0) { TakeSide(sides[i]); sides[i].rcShape++; }
	g_sideTaken[i] = sides[i].rc;
	return change != 0;
}

void FillLine(int i, int change)
{
	if (change != 0) TakeLine(lines[i]);
	g_lineTaken[i] = lines[i].rc;
}

// [rc4l] FillSectors for the sides and the lines.
void FillSides(const std::vector<int> &sideCandidates, const std::vector<int> &lineCandidates, bool scanned)
{
	int taken = 0;
	if (scanned)
	{
		ZX_ZONE("copy/side_take");
		for (int i : sideCandidates) taken += FillSide(i, SideChange(sides[i]));
		for (int i : lineCandidates) FillLine(i, LineChange(lines[i]));
	}
	else
	{
		static std::vector<int> sideChanges, lineChanges;
		{
			ZX_ZONE("copy/side_scan");
			ScanChanges(numsides, sideChanges, [](int i) { return SideChange(sides[i]); });
			ScanChanges(numlines, lineChanges, [](int i) { return LineChange(lines[i]); });
		}
		for (int i = 0; i < numsides; i++) if (sideChanges[i] != 0) taken += FillSide(i, sideChanges[i]);
		for (int i = 0; i < numlines; i++) if (lineChanges[i] != 0) FillLine(i, lineChanges[i]);
		for (int i : sideCandidates) g_sideTaken[i] = sides[i].rc;
		for (int i : lineCandidates) g_lineTaken[i] = lines[i].rc;
	}
	ZX_COUNT("copy/sides_taken", "sides", taken);
}

// [rc4l] Only a polyobject's vertices move; one that moved reshapes the polyobject's every side.
void FillPolyobjects()
{
	for (int p = 0; p < po_NumPolyobjs; p++)
	{
		FPolyObj &po = polyobjs[p];
		bool moved = false;
		for (unsigned i = 0; i < po.Vertices.Size(); i++)
		{
			vertex_t &v = *po.Vertices[i];
			if (VertexChange(v) == 0) continue;
			TakeVertex(v);
			moved = true;
		}
		if (moved) for (unsigned i = 0; i < po.Sidedefs.Size(); i++) po.Sidedefs[i]->rcShape++;
	}
}

std::vector<ActorRecord> g_actors;   // [rc4l] the last fill's actors, reused fill to fill
std::vector<int> g_firstIn;          // [rc4l] each sector's first record
std::vector<int> g_skyBoxes;         // [rc4l] each sector's floor and ceiling sky viewpoint records
int g_defaultSkyBox = -1;
unsigned int g_lastSerial = kNoId;   // [rc4l] the last id given
std::unordered_map<unsigned int, int> g_byId;   // [rc4l] each id's record, built by the first lookup after a fill
bool g_byIdBuilt = false;
int g_level = 0;   // [rc4l] which level the copy is of

// [rc4l] Actors each pool lane takes at a time.
static const int kActorChunk = 256;

// [rc4l] Every actor in thinker order, each given its id in that order, as the records will hold them.
void ListActors(std::vector<AActor *> &out)
{
	out.clear();
	TThinkerIterator<AActor> it;
	for (AActor *a; (a = it.Next()) != NULL; )
	{
		if (a->rcSerial == kNoId) a->rcSerial = g_lastSerial = ComputeNextSerial(g_lastSerial);
		// [rc4l] Its place noted while it is in cache, which LinkListed reads; a pass of its own cost a Complex Doom fight half a millisecond a tic.
		a->rcListed = (int)out.size();
		out.push_back(a);
	}
}

// [rc4l] Each sector's actors linked by their places in list, in its thinglist's order: its first in firstIn, each one's next in next; list is ListActors', whose places each actor holds.
void LinkListed(const std::vector<AActor *> &list, std::vector<int> &firstIn, std::vector<int> &next)
{
	firstIn.resize(numsectors);
	next.assign(list.size(), -1);
	RecordLinks links;
	for (int s = 0; s < numsectors; s++)
	{
		links.Begin(firstIn[s]);
		for (AActor *a = sectors[s].thinglist; a != NULL; a = a->snext)
		{
			const int r = a->rcListed;
			if (ComputeHoldsRecord(r, (int)list.size()) && list[r] == a) links.Add(r, next[r]);
		}
	}
}

std::vector<AActor *> g_listed;                    // [rc4l] the actors as the tic ended, in thinker order
std::vector<int> g_listedFirstIn, g_listedNext;   // [rc4l] and each sector's list of them

// [rc4l] Every actor in thinker order, then each sector's records linked in its thinglist's order, the list and links the tic's end made when they still hold (listed).
void FillActors(bool listed)
{
	ZX_ZONE("copy/actor_fill");   // [rc4l] the actor records taken
	g_byIdBuilt = false;
	static std::vector<AActor *> live;
	static std::vector<int> next;
	if (listed)
	{
		live.swap(g_listed);
		next.swap(g_listedNext);
		g_firstIn.swap(g_listedFirstIn);
	}
	else
	{
		{ ZX_ZONE("copy/actor_list"); ListActors(live); }
		{ ZX_ZONE("copy/actor_link"); LinkListed(live, g_firstIn, next); }
	}
	// [rc4l] The camera's read barrier settled before the lanes read it, since one finding it destroyed clears it.
	AActor *const camera = players[consoleplayer].camera;
	(void)camera;
	g_actors.resize(live.size());
	// [rc4l] Taken across the pool, each lane writing only its own actors and records, since Sunder MAP11's 6,029 took 0.9 ms of a browser's handoff.
	ZX_ZONE("copy/actor_take");
	auto take = [](int lo, int hi, int) {
		for (int i = lo; i < hi; i++)
		{
			live[i]->rcRecord = i;
			TakeActor(*live[i], g_actors[i]);
			g_actors[i].nextInSector = next[i];
		}
	};
	zx::jobs::Pool().ParallelChunks((int)live.size(), kActorChunk, take);
	ZX_COUNT("copy/actors", "actors", g_actors.size());
}

int RecordIndexOf(const AActor *a);

// [rc4l] Each sector's sky viewpoints and the level's default one, as records.
void FillSkyBoxes()
{
	g_skyBoxes.resize(numsectors * 2);
	for (int i = 0; i < numsectors; i++)
		for (int pos = 0; pos < 2; pos++) g_skyBoxes[i * 2 + pos] = RecordIndexOf(sectors[i].GetSkyBox(pos));
	g_defaultSkyBox = RecordIndexOf(level.DefaultSkybox);
}

// [rc4l] The first member two records hold differently, or NULL.
const char *ActorDiffers(const ActorRecord &a, const ActorRecord &b)
{
#define ZX_ACTOR_FIELD(m) if (!Same(a.m, b.m)) return #m;
#define ZX_ACTOR_DERIVED(type, m) if (!Same(a.m, b.m)) return #m;
#include "features/gamecopy/fields.h"
#undef ZX_ACTOR_FIELD
#undef ZX_ACTOR_DERIVED
	return NULL;
}

const char *PlayerDiffers(const PlayerRecord &a, const PlayerRecord &b)
{
#define ZX_PLAYER_FIELD(m) if (!Same(a.m, b.m)) return #m;
#define ZX_PLAYER_DERIVED(type, m) if (!Same(a.m, b.m)) return #m;
#include "features/gamecopy/fields.h"
#undef ZX_PLAYER_FIELD
#undef ZX_PLAYER_DERIVED
	return NULL;
}

LevelRecord g_level_;   // [rc4l] the level as the last fill took it
zx::postprocess::ShaderTable g_levelShaders;   // [Doruk-Aksoy] the level's post-process shader state the last fill took

void TakeLevel(LevelRecord &out)
{
#define ZX_LEVEL_FIELD(m) out.m = level.m;
#include "features/gamecopy/fields.h"
#undef ZX_LEVEL_FIELD
	out.sky1 = sky1texture;
	out.sky2 = sky2texture;
	out.endLevelDelay = GAME_GetEndLevelDelay () != 0;
	out.freeSpectate = CLIENTDEMO_IsInFreeSpectateMode ();
	if (g_levelShaders.generation != level.fua_postShaders.generation) g_levelShaders = level.fua_postShaders;
}

const char *LevelDiffers(const LevelRecord &a, const LevelRecord &b)
{
#define ZX_LEVEL_FIELD(m) if (!Same(a.m, b.m)) return #m;
#define ZX_LEVEL_DERIVED(type, m) if (!Same(a.m, b.m)) return #m;
#include "features/gamecopy/fields.h"
#undef ZX_LEVEL_FIELD
#undef ZX_LEVEL_DERIVED
	return NULL;
}

PlayerRecord g_players[MAXPLAYERS];   // [rc4l] the last fill's players
bool g_inGame[MAXPLAYERS];
std::vector<ViewSource> g_sources;    // [rc4l] the last fill's view sources
std::vector<CameraTexture> g_cameras;  // [rc4l] the last fill's camera textures

// [rc4l] The index of a live actor's record in this fill, -1 for none.
int RecordIndexOf(const AActor *a)
{
	return a != NULL && ComputeHoldsRecord(a->rcRecord, (int)g_actors.size()) && g_actors[a->rcRecord].who == a ? a->rcRecord : -1;
}

void AddLayer(void *layers, float r, float g, float b, float a)
{
	const BlendLayer l = { r, g, b, a };
	static_cast<std::vector<BlendLayer> *>(layers)->push_back(l);
}

void TakePlayer(player_t &live, PlayerRecord &out)
{
#define ZX_PLAYER_FIELD(m) out.m = live.m;
#include "features/gamecopy/fields.h"
#undef ZX_PLAYER_FIELD
	out.who = &live;
	out.number = int(&live - players);
	out.mo = RecordIndexOf(live.mo);
	AActor *const camera = live.camera;
	out.camera = camera != NULL ? RecordIndexOf(camera) : out.mo;
	out.lightPower = zx::viewsetup::kNoLightPower;
	if (live.mo != NULL)
		for (AInventory *inv = live.mo->Inventory; inv; inv = inv->Inventory)
		{
			if (inv->IsKindOf(RUNTIME_CLASS(APowerTorch))) out.lightPower = zx::viewsetup::kTorchPower;
			else if (inv->IsKindOf(RUNTIME_CLASS(APowerLightAmp))) out.lightPower = zx::viewsetup::kLightAmpPower;
		}
	out.hasWeapon = live.ReadyWeapon != NULL;
	out.weaponFOVScale = live.ReadyWeapon != NULL ? live.ReadyWeapon->FOVScale : 0.f;
	out.weaponType = live.ReadyWeapon != NULL ? live.ReadyWeapon->GetClass() : NULL;
	out.weaponYAdjust = live.ReadyWeapon != NULL ? live.ReadyWeapon->YAdjust : fixed_t(0);
	// [rc4l] Find, not operator[], which would make the layer it looks for.
	const pspdef_t *const weaponLayer = live.psprites.Find(ps_weapon);
	out.hudModel = live.ReadyWeapon != NULL && weaponLayer != NULL && gl_IsHUDModelFor(live.ReadyWeapon->GetClass(), weaponLayer->state);
	out.bobIn = P_WeaponBobInputs(&live);
	out.layers.clear();
	for (unsigned i = 0; i < live.psprites.Size(); i++) out.layers.push_back(live.psprites.Element(i));
	out.screenBlend.layers.clear();
	out.screenBlend.cap = 0.f;
	out.screenBlend.applies = V_PlayerBlendLayers(&live, 0.5f, 175, AddLayer, &out.screenBlend.layers, out.screenBlend.cap);
}

// [rc4l] A player naming no camera sees through its body, which D_Display also writes back, so the copy only reads.
void FillPlayers()
{
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		g_inGame[i] = playeringame[i];
		if (g_inGame[i]) TakePlayer(players[i], g_players[i]);
	}
}

ViewSource TakeViewSource(int record, AActor *actor)
{
	ViewSource v;
	v.record = record;
	DEarthquake::StaticGetQuakes(actor, v.quakes);
	v.chaseX = v.chaseY = v.chaseZ = 0;
	v.chaseSector = NULL;
	memset(&v.weaponVis, 0, sizeof(v.weaponVis));
	v.chased = (players[consoleplayer].cheats & CF_CHASECAM) || actor->health <= 0;
	if (v.chased) P_AimCamera(actor, v.chaseX, v.chaseY, v.chaseZ, v.chaseSector);
	v.unrestricted = P_IsSpectatorUnrestricted(actor);
	// [rc4l] The weapon's style starts from the actor's own and its inventory alters it.
	v.weaponVis.RenderStyle = actor->RenderStyle;
	v.weaponVis.alpha = actor->alpha;
	v.weaponVis.colormap = NULL;
	v.altersWeapon = actor->Inventory != NULL;
	if (v.altersWeapon) actor->Inventory->AlterWeaponSprite(&v.weaponVis);
	return v;
}

// [rc4l] Every player's camera and every camera texture's viewpoint, each once.
void FillViewSources()
{
	g_sources.clear();
	const auto add = [](AActor *actor)
	{
		const int r = RecordIndexOf(actor);
		if (r < 0) return;
		for (const ViewSource &v : g_sources) if (v.record == r) return;
		g_sources.push_back(TakeViewSource(r, actor));
	};
	for (int i = 0; i < MAXPLAYERS; i++)
		if (g_inGame[i]) add(players[i].camera != NULL ? (AActor *)players[i].camera : players[i].mo);
	g_cameras.clear();
	for (FCanvasTextureInfo *p = FCanvasTextureInfo::GetList(); p != NULL; p = p->Next)
	{
		add(p->Viewpoint);
		const CameraTexture c = { p->Texture, RecordIndexOf(p->Viewpoint), p->FOV, p->PicNum };
		g_cameras.push_back(c);
	}
}

bool SameSource(const ViewSource &a, const ViewSource &b)
{
	if (a.quakes.Size() != b.quakes.Size()) return false;
	for (unsigned i = 0; i < a.quakes.Size(); i++) if (memcmp(&a.quakes[i], &b.quakes[i], sizeof(FQuakeState)) != 0) return false;
	return a.record == b.record && a.chased == b.chased &&
		Same(a.chaseX, b.chaseX) && Same(a.chaseY, b.chaseY) && Same(a.chaseZ, b.chaseZ) && a.chaseSector == b.chaseSector && a.unrestricted == b.unrestricted &&
		a.altersWeapon == b.altersWeapon && a.weaponVis.colormap == b.weaponVis.colormap && Same(a.weaponVis.alpha, b.weaponVis.alpha) &&
		Same(a.weaponVis.RenderStyle, b.weaponVis.RenderStyle) && a.weaponVis.bFixedColormap == b.weaponVis.bFixedColormap;
}

// [rc4l] Writes a blended value into the copy, bumping the counter only when the copy moved.
bool Put(zx::Fixed &rc, zx::Fixed value)
{
	if (rc.Raw() == value.Raw()) return false;
	rc = value;
	return true;
}

} // namespace

Blends &GatheringBlends()
{
	return g_gathering;
}

void TakeActor(AActor &live, ActorRecord &out)
{
#define ZX_ACTOR_FIELD(m) out.m = live.m;
#include "features/gamecopy/fields.h"
#undef ZX_ACTOR_FIELD
	out.who = &live;
	if (live.rcSerial == kNoId) live.rcSerial = g_lastSerial = ComputeNextSerial(g_lastSerial);
	out.id = live.rcSerial;
	out.type = live.GetClass();
	out.target = live.target;
	out.nextInSector = -1;
	int sprite = live.sprite;
	fixed_t scaleX = live.scaleX, scaleY = live.scaleY;
	if (live.player != NULL) P_CheckPlayerSprite(&live, sprite, scaleX, scaleY);
	out.shownSprite = sprite;
	out.shownScaleX = scaleX;
	out.shownScaleY = scaleY;
	out.visible = live.IsVisibleToPlayer();
	out.isPlayer = live.player != NULL;
	out.playerIndex = live.player != NULL ? int(live.player - players) : -1;
	out.inventory = live.IsKindOf(RUNTIME_CLASS(AInventory));
	player_t &me = players[consoleplayer];
	out.cameraIcon = !(me.cheats & CF_CHASECAM) && me.camera != NULL && me.camera->player != NULL && &live == me.camera->player->pIcon;
	out.bobTic = live.FloatBobPhase + level.maptime;
	out.attackRadius = live.GetAttackRadius();
	out.attackHeight = live.GetAttackHeight();
	const zx::gamecopy::TintSubject tint = zx::gamecopy::TintSubjectOf(&live);
	out.playerBody = tint.playerBody;
	out.tintActive = tint.active;
	out.tintTexture = tint.texture;
}

const ActorRecord *Actors(int &count)
{
	count = (int)g_actors.size();
	return g_actors.empty() ? NULL : &g_actors[0];
}

const ActorRecord *Record(int n)
{
	return ComputeHoldsRecord(n, (int)g_actors.size()) ? &g_actors[n] : NULL;
}

int IndexOf(const ActorRecord *r)
{
	return r != NULL && !g_actors.empty() && r >= &g_actors[0] && r < &g_actors[0] + g_actors.size() ? int(r - &g_actors[0]) : -1;
}

const ActorRecord *RecordOf(const AActor *actor)
{
	return Record(RecordIndexOf(actor));
}

const ActorRecord *RecordById(unsigned int id)
{
	if (id == kNoId) return NULL;
	if (!g_byIdBuilt)
	{
		g_byId.clear();
		for (size_t i = 0; i < g_actors.size(); i++) g_byId[g_actors[i].id] = (int)i;
		g_byIdBuilt = true;
	}
	const auto it = g_byId.find(id);
	if (it != g_byId.end()) return &g_actors[it->second];
	ZX_COUNT("copy/stale_ids", "lookups", 1);   // [rc4l] a cached id whose actor the copy no longer holds
	return NULL;
}

int CopyTic()
{
	return g_filled.tic;
}

// [rc4l] The tic and the server packets applied as of now, which the copy is taken at.
FillStamp StampOfNow()
{
	FillStamp s;
	s.tic = gametic;
	s.packets = zx::netclient::ParsedPackets();
	return s;
}

int CopyLevel()
{
	return g_level;
}

const LevelRecord &Level()
{
	return g_level_;
}

const zx::postprocess::ShaderTable &LevelShaders() { return g_levelShaders; }

namespace {

zx::postprocess::DrawnShaders DrawnPostShaders()
{
	const zx::postprocess::DrawnShaders drawn = { &g_levelShaders, g_level_.maptime };
	return drawn;
}

// [Doruk-Aksoy] features/postprocess sits below the copy, so the shader state the frame draws reaches it through what this installs.
const bool g_bDrawnShadersInstalled = ( zx::postprocess::SetDrawnShaders( &DrawnPostShaders ), true );

} // namespace

const std::vector<CameraTexture> &CameraTextures()
{
	return g_cameras;
}

const PlayerRecord *Player(int n)
{
	return n >= 0 && n < MAXPLAYERS && g_inGame[n] ? &g_players[n] : NULL;
}

const ViewSource *ViewSourceOf(int record)
{
	for (const ViewSource &v : g_sources) if (v.record == record) return &v;
	return NULL;
}

int g_viewCamera = -1;   // [rc4l] the record R_SetupFrame last set a view up from

const ActorRecord *ViewCamera()
{
	return Record(g_viewCamera);
}

void SetViewCamera(int record)
{
	g_viewCamera = record;
}

const ActorRecord *CameraOf(int n)
{
	const PlayerRecord *p = Player(n);
	return p != NULL ? Record(p->camera) : NULL;
}

const pspdef_t *Layer(const PlayerRecord &player, int layer)
{
	for (const pspdef_t &l : player.layers) if (l.layer == layer) return &l;
	return NULL;
}

void AddScreenBlend(const ScreenBlendLayers &s, float blend[4])
{
	if (!s.applies) return;
	for (const BlendLayer &l : s.layers) V_AddBlend(l.r, l.g, l.b, l.a, blend);
	if (blend[3] > s.cap) blend[3] = s.cap;
}

int SkyBoxOf(const sector_t *sector, int plane)
{
	const ptrdiff_t i = sector - sectors;
	return i >= 0 && i * 2 + 1 < (ptrdiff_t)g_skyBoxes.size() ? g_skyBoxes[i * 2 + plane] : -1;
}

int DefaultSkyBox()
{
	return g_defaultSkyBox;
}

int FirstActorIn(const sector_t *sector)
{
	const ptrdiff_t i = sector - sectors;
	return i >= 0 && i < (ptrdiff_t)g_firstIn.size() ? g_firstIn[i] : -1;
}

fixed_t BobOffset(const ActorRecord &a, fixed_t ticfrac)
{
	return (a.flags2 & MF2_FLOATBOB) ? AActor::BobOffsetAt(a.bobTic, ticfrac) : 0;
}

void InitLevel()
{
	g_levelLoaded = true;
	for (int i = 0; i < numsectors; i++) { TakeSector(sectors[i]); sectors[i].rcShape = sectors[i].rcPlacement = sectors[i].rcLook = 0; }
	for (int i = 0; i < numsides; i++) { TakeSide(sides[i]); sides[i].rcShape = 0; }
	for (int i = 0; i < numlines; i++) TakeLine(lines[i]);
	for (int i = 0; i < numvertexes; i++) { TakeVertex(vertexes[i]); vertexes[i].rcDirty = true; }
	g_blends.Clear();
	g_gathering.Clear();
	KeepAllTaken();
	// [Doruk-Aksoy] The sides a save restores after this are written without the setters, so the first scan of a level compares every side.
	ComputeLogReset(g_sideLog, numsides, true);
	g_sideSlice = 0;
	ResetDecalSides();
	g_prescan.stamp.found = false;
	g_actors.clear();
	g_byIdBuilt = false;
	g_level++;
	g_firstIn.clear();
	g_skyBoxes.clear();
	g_defaultSkyBox = -1;
	g_sources.clear();
	g_cameras.clear();
	g_viewCamera = -1;
	for (int i = 0; i < MAXPLAYERS; i++) g_inGame[i] = false;
	g_filled = FillStamp();
	TakeLevel(g_level_);
	InitXFloors();
	InitLights();
	InitSeen();
	InitPolyBSPs();
	// [rc4l] features/handoff: room for what a level's scripts add while its tics run, so the arrays a frame reads do not move under it.
	TexMan.ReserveHeadroom(1024);
	for (int i = 0; i < NUM_TRANSLATION_TABLES; i++) translationtables[i].Grow(256);
}

void ForgetLevel()
{
	g_levelLoaded = false;
	ComputeLogReset(g_sideLog, 0, true);
	ResetDecalSides();
	ForgetXFloors();
	InitPolyBSPs();
	InitLights();
	g_firstIn.clear();
}

void FillMap()
{
	if (ComputeFillDue(StampOfNow(), g_filled)) FillNow();
}

zx::frame::OracleTally g_prescanCheck;   // [rc4l] fua_copy_prescan_check's count since the last report
int g_prescanListed = 0;                 // [rc4l] and how many of its fills took the actor list too

// [rc4l] One thing the tic's end scan missed counted, the first few described.
void PrescanMissed(const char *what, int index)
{
	if (zx::frame::ComputeOracleDiffer(g_prescanCheck, 6)) zx::frame::ComputeOracleNote(g_prescanCheck, "  %s %d changed after the tic's end scan or past it\n", what, index);
}

// [rc4l] An item the whole-map scan would take that is no candidate, an actor list or sector links unlike the game's now, a side holding decals the scan did not find, a seen bit unlike the live map's.
void CheckPrescan(const std::vector<int> &sectorCandidates, const std::vector<int> &sideCandidates, const std::vector<int> &lineCandidates, bool listed)
{
	g_prescanCheck.checked++;
	for (int i = 0; i < numsectors; i++)
		if (SectorChange(sectors[i]) != 0 && !std::binary_search(sectorCandidates.begin(), sectorCandidates.end(), i)) PrescanMissed("sector", i);
	for (int i = 0; i < numsides; i++)
		if (SideChange(sides[i]) != 0 && !std::binary_search(sideCandidates.begin(), sideCandidates.end(), i)) PrescanMissed("side", i);
	for (int i = 0; i < numlines; i++)
		if (LineChange(lines[i]) != 0 && !std::binary_search(lineCandidates.begin(), lineCandidates.end(), i)) PrescanMissed("line", i);
	const int decals = DecalSidesMissed();
	if (decals > 0) PrescanMissed("sides holding decals, count", decals);
	const int seen = SeenMissed();
	if (seen > 0) PrescanMissed("seen bits, count", seen);
	if (listed && LightListsMissed()) PrescanMissed("subsector light lists, subsectors", numsubsectors);
	if (!listed) return;
	g_prescanListed++;
	static std::vector<AActor *> now;
	static std::vector<int> firstIn, next;
	ListActors(now);
	if (now != g_listed) { PrescanMissed("actor list, length", (int)now.size()); return; }
	LinkListed(now, firstIn, next);
	if (firstIn != g_listedFirstIn || next != g_listedNext) PrescanMissed("sector links, actors", (int)now.size());
}

// [rc4l] When a scan is found or a fill is taken: this fill of this level after this many effects, over the map as it is.
PrescanStamp StampNow(bool found)
{
	const PrescanStamp s = { found, g_fills, g_level, zx::handoff::EffectsRun(), g_actorLists, numsectors, numsides, numlines };
	return s;
}

void NoteSideChanged(const side_t *side)
{
	ComputeLogNote(g_sideLog, ComputeIndexIn((uintptr_t)side, (uintptr_t)sides, sizeof(side_t), numsides));
}

void NoteMapLoaded()
{
	g_sideLog.whole = true;
	ResetDecalSides();
}

// [Doruk-Aksoy] The sides changed since the copy last took them: those noted that differ, and any in this tic's slice that differ unnoted, which are counted.
static void ListSideChanges(std::vector<int> &out)
{
	const auto changed = [](int i) { return SideChangeFrom(g_sideTaken[i], sides[i]) != 0; };
	if (g_sideLog.whole || g_sideLog.noted.size() != (size_t)numsides)
	{
		ListChanges(numsides, out, changed);
		return;
	}
	out.clear();
	for (int i : g_sideLog.list) if (changed(i)) out.push_back(i);
	int lo, hi, untracked = 0;
	ComputeVerifySlice(numsides, kSideSlice, g_sideSlice, lo, hi);
	for (int i = lo; i < hi; i++)
		if (!g_sideLog.noted[i] && changed(i))
		{
			out.push_back(i);
			untracked++;
		}
	ZX_COUNT("copy/sides_untracked", "sides", untracked);
}

void ScanAtTicEnd()
{
	ZX_ZONE("copy/prescan");
	g_prescan.stamp.found = false;
	if (g_sectorTaken.size() != (size_t)numsectors || g_sideTaken.size() != (size_t)numsides || g_lineTaken.size() != (size_t)numlines) return;
	// [rc4l] Each part timed on its own, since in a Complex Doom fight the whole was a quarter of the tic.
	{ ZX_ZONE("copy/prescan_sectors"); ListChanges(numsectors, g_prescan.sectors, [](int i) { return SectorChangeFrom(g_sectorTaken[i], sectors[i]) != 0; }); }
	{ ZX_ZONE("copy/prescan_sides"); ListSideChanges(g_prescan.sides); }
	{ ZX_ZONE("copy/prescan_lines"); ListChanges(numlines, g_prescan.lines, [](int i) { return LineChangeFrom(g_lineTaken[i], lines[i]) != 0; }); }
	{ ZX_ZONE("copy/prescan_actors"); ListActors(g_listed); }
	{ ZX_ZONE("copy/prescan_links"); LinkListed(g_listed, g_listedFirstIn, g_listedNext); }
	{ ZX_ZONE("copy/prescan_decals"); ScanDecalSides(); }
	{ ZX_ZONE("copy/prescan_seen"); ScanSeen(); }
	{ ZX_ZONE("copy/prescan_lights"); ScanLights(); }
	g_prescan.stamp = StampNow(true);
}

void FillNow()
{
	// [rc4l] Between a level's data being freed and the next set up, as at the full console endgame leaves, there is nothing to copy.
	if (!g_levelLoaded) return;
	g_filled = StampOfNow();
	ZX_SEAM("copy/fill", Handoff);
	// [rc4l] Whether the tic's end found the map's changes, which spares the handoff comparing the whole map (Sunder MAP16's 62,000 sides) while the frames wait.
	const bool scanned = ComputePrescanFresh(g_prescan.stamp, StampNow(true), zx::net::InClientMode());
	const bool listed = ComputeActorScanFresh(g_prescan.stamp, StampNow(true), zx::net::InClientMode());
	g_prescan.stamp.found = false;
	g_fills++;
	ZX_COUNT("copy/prescanned", "fills", scanned ? 1 : 0);
	if (g_sectorTaken.size() != (size_t)numsectors || g_sideTaken.size() != (size_t)numsides || g_lineTaken.size() != (size_t)numlines) KeepAllTaken();
	static std::vector<int> seenSectors, seenLines, blendedSectors, blendedSides, sectorCandidates, sideCandidates, lineCandidates;
	// [rc4l] Each step timed on its own, since on Sunder MAP16 the fill was 5 ms a tic and only the actor, light, decal and particle steps said where.
	{ ZX_ZONE("copy/seen_merge"); MergeSeen(scanned, seenSectors, seenLines); }
	blendedSectors.clear();
	blendedSides.clear();
	for (const PlaneBlend &b : g_blends.planes) blendedSectors.push_back(b.sector);
	for (const ScrollBlend &b : g_blends.scrolls) blendedSectors.push_back(b.sector);
	for (const WallBlend &b : g_blends.walls) blendedSides.push_back(b.side);
	ComputeTakeCandidates(scanned ? g_prescan.sectors : kNoneScanned, blendedSectors, seenSectors, numsectors, sectorCandidates);
	ComputeTakeCandidates(scanned ? g_prescan.sides : kNoneScanned, blendedSides, kNoneScanned, numsides, sideCandidates);
	ComputeTakeCandidates(scanned ? g_prescan.lines : kNoneScanned, kNoneScanned, seenLines, numlines, lineCandidates);
	if (fua_copy_prescan_check && scanned) CheckPrescan(sectorCandidates, sideCandidates, lineCandidates, listed);
	{ ZX_ZONE("copy/sector_fill"); FillSectors(sectorCandidates, scanned); }
	{ ZX_ZONE("copy/xfloor_fill"); FillXFloors(); }
	{ ZX_ZONE("copy/side_fill"); FillSides(sideCandidates, lineCandidates, scanned); }
	{ ZX_ZONE("copy/polyobj_fill"); FillPolyobjects(); FillPolyBSPs(); }
	TakeLevel(g_level_);
	FillActors(listed);
	FillLights(listed);
	FillParticles();
	FillDecals(scanned);
	{ ZX_ZONE("copy/view_fill"); FillSkyBoxes(); FillPlayers(); FillViewSources(); }
	g_gathering.Clear();
	{ ZX_ZONE("copy/interp_fill"); interpolator.CopyForRender(); }
	g_blends.planes.swap(g_gathering.planes);
	g_blends.scrolls.swap(g_gathering.scrolls);
	g_blends.walls.swap(g_gathering.walls);
	g_blends.polys.swap(g_gathering.polys);
	// [Doruk-Aksoy] Every side's change up to now is taken, whichever way the sides were found, so the log starts over.
	if (g_sideLog.noted.size() != (size_t)numsides) ComputeLogReset(g_sideLog, numsides, false);
	else ComputeLogClear(g_sideLog);
}

void BlendMap(fixed_t smoothratio)
{
	ZX_SEAM("copy/blend", Main);
	for (const PlaneBlend &b : g_blends.planes)
	{
		sector_t &s = sectors[b.sector];
		const int pos = b.ceiling ? sector_t::ceiling : sector_t::floor;
		secplane_core_t &plane = b.ceiling ? s.rc.ceilingplane : s.rc.floorplane;
		bool moved = Put(plane.d, ComputeBlendFixed(b.oldD, b.curD, smoothratio));
		moved |= Put(s.rc.planes[pos].TexZ, ComputeBlendFixed(b.oldTexZ, b.curTexZ, smoothratio));
		if (moved) Bump(s, kShape);
	}
	for (const ScrollBlend &b : g_blends.scrolls)
	{
		sector_t &s = sectors[b.sector];
		FTransform &xform = s.rc.planes[b.ceiling ? sector_t::ceiling : sector_t::floor].xform;
		bool moved = Put(xform.xoffs, ComputeBlendFixed(b.oldX, b.curX, smoothratio));
		moved |= Put(xform.yoffs, ComputeBlendFixed(b.oldY, b.curY, smoothratio));
		if (moved) Bump(s, kPlacement);
	}
	for (const WallBlend &b : g_blends.walls)
	{
		side_t &d = sides[b.side];
		bool moved = Put(d.rc.textures[b.part].xoffset, ComputeBlendFixed(b.oldX, b.curX, smoothratio));
		moved |= Put(d.rc.textures[b.part].yoffset, ComputeBlendFixed(b.oldY, b.curY, smoothratio));
		if (moved) d.rcShape++;
	}
	for (const PolyBlend &b : g_blends.polys)
		for (size_t i = 0; i < b.vertex.size(); i++)
		{
			vertex_t &v = vertexes[b.vertex[i]];
			Put(v.rc.x, ComputeBlendFixed(b.oldXY[i * 2], b.curXY[i * 2], smoothratio));
			Put(v.rc.y, ComputeBlendFixed(b.oldXY[i * 2 + 1], b.curXY[i * 2 + 1], smoothratio));
		}
	BlendXFloors();
	BlendPolyBSPs();
}

// [rc4l] The seen bits the renderer sets on the live map between tics (ML_MAPPED, SECF_DRAWN), which no tic changes, taken as the copy's while a check compares.
int SectorChangeIgnoringSeen(sector_t &s)
{
	const auto saved = s.rc.MoreFlags;
	s.rc.MoreFlags = decltype(saved)((saved & ~SECF_DRAWN) | (s.MoreFlags & SECF_DRAWN));
	const int change = SectorChange(s);
	s.rc.MoreFlags = saved;
	return change;
}
int LineChangeIgnoringSeen(line_t &l)
{
	const auto saved = l.rc.flags;
	l.rc.flags = decltype(saved)((saved & ~ML_MAPPED) | (l.flags & ML_MAPPED));
	const int change = LineChange(l);
	l.rc.flags = saved;
	return change;
}

bool CopyCheckOn()
{
	return fua_copy_check;
}

void CheckBeforeViews()
{
	if (!fua_copy_check) return;
	// [rc4l] What a blend moves is held to it after the interpolations instead.
	std::vector<char> blendedSector(numsectors, 0), blendedSide(numsides, 0);
	for (const PlaneBlend &b : g_blends.planes) blendedSector[b.sector] = 1;
	for (const ScrollBlend &b : g_blends.scrolls) blendedSector[b.sector] = 1;
	for (const WallBlend &b : g_blends.walls) blendedSide[b.side] = 1;
	for (int i = 0; i < numsectors; i++)
	{
		g_before.checked++;
		if (blendedSector[i] || SectorChangeIgnoringSeen(sectors[i]) == 0) continue;
		if (zx::frame::ComputeOracleDiffer(g_before, 6))
			zx::frame::ComputeOracleNote(g_before, "  sector %d's %s is not the live sector's (MoreFlags %#x, Flags %#x apart)\n", i, SectorDiffers(sectors[i]),
				unsigned(sectors[i].rc.MoreFlags ^ sectors[i].MoreFlags), unsigned(sectors[i].rc.Flags ^ sectors[i].Flags));
	}
	for (int i = 0; i < numsides; i++)
	{
		g_before.checked++;
		if (blendedSide[i] || SideChange(sides[i]) == 0) continue;
		if (zx::frame::ComputeOracleDiffer(g_before, 6)) zx::frame::ComputeOracleNote(g_before, "  side %d's %s is not the live side's\n", i, SideDiffers(sides[i]));
	}
	for (int i = 0; i < numlines; i++)
	{
		g_before.checked++;
		if (LineChangeIgnoringSeen(lines[i]) != 0 && zx::frame::ComputeOracleDiffer(g_before, 6))
			zx::frame::ComputeOracleNote(g_before, "  line %d's %s is not the live line's (flags %#x apart)\n", i, LineDiffers(lines[i]), unsigned(lines[i].rc.flags ^ lines[i].flags));
	}
	// [rc4l] Each live actor against its record, in the order the fill took them, the list links being the fill's own.
	TThinkerIterator<AActor> it;
	ActorRecord fresh;
	int n = 0;
	for (AActor *a; (a = it.Next()) != NULL; n++)
	{
		g_before.checked++;
		const char *why = "record";
		if (ComputeHoldsRecord(n, (int)g_actors.size()) && g_actors[n].who == a)
		{
			TakeActor(*a, fresh);
			fresh.nextInSector = g_actors[n].nextInSector;
			why = ActorDiffers(g_actors[n], fresh);
		}
		if (why != NULL && zx::frame::ComputeOracleDiffer(g_before, 6))
			zx::frame::ComputeOracleNote(g_before, "  actor %d (%s)'s %s is not the live actor's\n", n, a->GetClass()->TypeName.GetChars(), why);
	}
	g_before.checked++;
	if (n != (int)g_actors.size() && zx::frame::ComputeOracleDiffer(g_before, 6))
		zx::frame::ComputeOracleNote(g_before, "  the copy holds %d actors and the game %d\n", (int)g_actors.size(), n);
	// [rc4l] Each player and view source against what the live game gives now.
	PlayerRecord freshPlayer;
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		g_before.checked++;
		const char *differs = g_inGame[i] != playeringame[i] ? "presence" : NULL;
		if (differs == NULL && g_inGame[i])
		{
			TakePlayer(players[i], freshPlayer);
			differs = PlayerDiffers(g_players[i], freshPlayer);
		}
		if (differs != NULL && zx::frame::ComputeOracleDiffer(g_before, 6))
			zx::frame::ComputeOracleNote(g_before, "  player %d's %s is not the live player's\n", i, differs);
	}
	g_before.checked++;
	LevelRecord freshLevel;
	TakeLevel(freshLevel);
	if (const char *differs = LevelDiffers(g_level_, freshLevel))
		if (zx::frame::ComputeOracleDiffer(g_before, 6)) zx::frame::ComputeOracleNote(g_before, "  the level's %s is not the live level's\n", differs);
	for (const ViewSource &v : g_sources)
	{
		g_before.checked++;
		if (SameSource(v, TakeViewSource(v.record, (AActor *)const_cast<void *>(g_actors[v.record].who)))) continue;
		if (zx::frame::ComputeOracleDiffer(g_before, 6)) zx::frame::ComputeOracleNote(g_before, "  the view from record %d is not the live game's\n", v.record);
	}
}

void CheckAfterInterpolation()
{
	if (!fua_copy_check || !interpolator.didInterp) return;
	for (const PlaneBlend &b : g_blends.planes)
	{
		const sector_t &s = sectors[b.sector];
		const int pos = b.ceiling ? sector_t::ceiling : sector_t::floor;
		g_after.checked++;
		if (Same(s.rc.GetSecPlane(pos), static_cast<const secplane_core_t &>(s.GetSecPlane(pos))) && Same(s.rc.planes[pos].TexZ, s.planes[pos].TexZ)) continue;
		if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "  sector %d's blended %s is not where the interpolation put it\n", b.sector, b.ceiling ? "ceiling" : "floor");
	}
	for (const ScrollBlend &b : g_blends.scrolls)
	{
		const sector_t &s = sectors[b.sector];
		const int pos = b.ceiling ? sector_t::ceiling : sector_t::floor;
		g_after.checked++;
		if (Same(s.rc.planes[pos].xform, s.planes[pos].xform)) continue;
		if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "  sector %d's blended scroll is not where the interpolation put it\n", b.sector);
	}
	for (const WallBlend &b : g_blends.walls)
	{
		g_after.checked++;
		if (Same(sides[b.side].rc.textures[b.part], sides[b.side].textures[b.part])) continue;
		if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "  side %d's blended scroll is not where the interpolation put it\n", b.side);
	}
	for (const PolyBlend &b : g_blends.polys)
		for (size_t i = 0; i < b.vertex.size(); i++)
		{
			g_after.checked++;
			if (VertexChange(vertexes[b.vertex[i]]) == 0) continue;
			if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "  vertex %d's blended place is not where the interpolation put it\n", b.vertex[i]);
		}
}

}} // namespace zx::gamecopy

// [rc4l] The seen bits the walks have set on the live map, which the automap, savegames and bots read, so a renderer that stops setting them shows here.
CCMD(fua_seen_count)
{
	int mapped = 0, drawn = 0;
	for (int i = 0; i < numlines; i++) if (lines[i].flags & ML_MAPPED) mapped++;
	for (int i = 0; i < numsectors; i++) if (sectors[i].MoreFlags & SECF_DRAWN) drawn++;
	Printf("seen: %d lines mapped, %d sectors drawn\n", mapped, drawn);
}

// [rc4l] What fua_copy_prescan_check found since the last report.
CCMD(fua_copy_prescan_report)
{
	using namespace zx::gamecopy;
	Printf("prescan: %d fills checked, %d with the actor list, %d missed\n%s", g_prescanCheck.checked, g_prescanListed, g_prescanCheck.differed,
		g_prescanCheck.firsts.c_str());
	zx::frame::ComputeOracleReset(g_prescanCheck);
	g_prescanListed = 0;
}

// [rc4l] What fua_copy_check found since the last report: the copy against the live game before the views, and its blends against the interpolations.
CCMD(fua_copy_report)
{
	using namespace zx::gamecopy;
	Printf("copy before views: %d compared, %d differed\n%s", g_before.checked, g_before.differed, g_before.firsts.c_str());
	Printf("copy blends: %d compared, %d differed\n%s", g_after.checked, g_after.differed, g_after.firsts.c_str());
	zx::frame::ComputeOracleReset(g_before);
	zx::frame::ComputeOracleReset(g_after);
}
