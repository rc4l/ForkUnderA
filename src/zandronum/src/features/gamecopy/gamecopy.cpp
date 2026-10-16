// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/gamecopy.h"
#include "features/frame/computation/oracle_compute.h"
#include "features/handoff/seam.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "doomtype.h"
#include "p_local.h"
#include "po_man.h"
#include "r_defs.h"
#include "r_state.h"
#include "r_data/r_interpolate.h"

#include <cstring>
#include <type_traits>
#include <vector>

// [rc4l] Holds the render copy to the live game while tics run inline: before each frame's views, and after each view's interpolations.
CVAR(Bool, fua_copy_check, false, 0)

namespace zx { namespace gamecopy {

namespace {

// [rc4l] What a change to a member means to the renderer's caches.
enum { kShape = 1, kPlacement = 2, kLook = 4 };

Blends g_blends;      // [rc4l] what frames blend, from the last fill
Blends g_gathering;   // [rc4l] what the interpolations add while a fill gathers
zx::frame::OracleTally g_before, g_after;

// [rc4l] Each member type compared by its value, never by the padding beside it.
bool Same(const zx::Fixed &a, const zx::Fixed &b) { return a.Raw() == b.Raw(); }
template <class T> typename std::enable_if<std::is_integral<T>::value || std::is_pointer<T>::value, bool>::type Same(T a, T b) { return a == b; }
bool Same(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }
bool Same(FTextureID a, FTextureID b) { return a == b; }
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

int SectorChange(const sector_t &s)
{
	int change = 0;
#define ZX_COPY_FIELD(kind, member, group) ZX_CHANGE_##kind(member, group)
#define ZX_CHANGE_sector(m, g) change |= Change##g(s.rc.m, s.m);
#define ZX_CHANGE_side(m, g)
#define ZX_CHANGE_line(m, g)
#define ZX_CHANGE_vertex(m, g)
#include "features/gamecopy/fields.h"
#undef ZX_CHANGE_sector
#undef ZX_CHANGE_side
#define ZX_CHANGE_sector(m, g)
#define ZX_CHANGE_side(m, g) change |= Change##g(d.rc.m, d.m);
	return change;
}

int SideChange(const side_t &d)
{
	int change = 0;
#include "features/gamecopy/fields.h"
	return change;
}
#undef ZX_CHANGE_side
#undef ZX_CHANGE_line
#define ZX_CHANGE_side(m, g)
#define ZX_CHANGE_line(m, g) change |= Change##g(l.rc.m, l.m);

int LineChange(const line_t &l)
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
#undef ZX_COPY_FIELD
#undef ZX_CHANGE_sector
#undef ZX_CHANGE_side
#undef ZX_CHANGE_line
#undef ZX_CHANGE_vertex

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

void FillSectors()
{
	for (int i = 0; i < numsectors; i++)
	{
		sector_t &s = sectors[i];
		const int change = SectorChange(s);
		if (change == 0) continue;
		TakeSector(s);
		Bump(s, change);
	}
}

void FillSides()
{
	for (int i = 0; i < numsides; i++)
	{
		side_t &d = sides[i];
		if (SideChange(d) == 0) continue;
		TakeSide(d);
		d.rcShape++;
	}
	for (int i = 0; i < numlines; i++)
		if (LineChange(lines[i]) != 0) TakeLine(lines[i]);
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

void InitLevel()
{
	for (int i = 0; i < numsectors; i++) { TakeSector(sectors[i]); sectors[i].rcShape = sectors[i].rcPlacement = sectors[i].rcLook = 0; }
	for (int i = 0; i < numsides; i++) { TakeSide(sides[i]); sides[i].rcShape = 0; }
	for (int i = 0; i < numlines; i++) TakeLine(lines[i]);
	for (int i = 0; i < numvertexes; i++) { TakeVertex(vertexes[i]); vertexes[i].rcDirty = true; }
	g_blends.Clear();
	g_gathering.Clear();
}

void FillMap()
{
	ZX_SEAM("copy/fill", Handoff);
	FillSectors();
	FillSides();
	FillPolyobjects();
	g_gathering.Clear();
	interpolator.CopyForRender();
	g_blends.planes.swap(g_gathering.planes);
	g_blends.scrolls.swap(g_gathering.scrolls);
	g_blends.walls.swap(g_gathering.walls);
	g_blends.polys.swap(g_gathering.polys);
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
		if (blendedSector[i] || SectorChange(sectors[i]) == 0) continue;
		if (zx::frame::ComputeOracleDiffer(g_before, 6)) zx::frame::ComputeOracleNote(g_before, "sector %d's copy is not the live sector", i);
	}
	for (int i = 0; i < numsides; i++)
	{
		g_before.checked++;
		if (blendedSide[i] || SideChange(sides[i]) == 0) continue;
		if (zx::frame::ComputeOracleDiffer(g_before, 6)) zx::frame::ComputeOracleNote(g_before, "side %d's copy is not the live side", i);
	}
	for (int i = 0; i < numlines; i++)
	{
		g_before.checked++;
		if (LineChange(lines[i]) != 0 && zx::frame::ComputeOracleDiffer(g_before, 6)) zx::frame::ComputeOracleNote(g_before, "line %d's copy is not the live line", i);
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
		if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "sector %d's blended %s is not where the interpolation put it", b.sector, b.ceiling ? "ceiling" : "floor");
	}
	for (const ScrollBlend &b : g_blends.scrolls)
	{
		const sector_t &s = sectors[b.sector];
		const int pos = b.ceiling ? sector_t::ceiling : sector_t::floor;
		g_after.checked++;
		if (Same(s.rc.planes[pos].xform, s.planes[pos].xform)) continue;
		if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "sector %d's blended scroll is not where the interpolation put it", b.sector);
	}
	for (const WallBlend &b : g_blends.walls)
	{
		g_after.checked++;
		if (Same(sides[b.side].rc.textures[b.part], sides[b.side].textures[b.part])) continue;
		if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "side %d's blended scroll is not where the interpolation put it", b.side);
	}
	for (const PolyBlend &b : g_blends.polys)
		for (size_t i = 0; i < b.vertex.size(); i++)
		{
			g_after.checked++;
			if (VertexChange(vertexes[b.vertex[i]]) == 0) continue;
			if (zx::frame::ComputeOracleDiffer(g_after, 6)) zx::frame::ComputeOracleNote(g_after, "vertex %d's blended place is not where the interpolation put it", b.vertex[i]);
		}
}

}} // namespace zx::gamecopy

// [rc4l] What fua_copy_check found since the last report: the copy against the live game before the views, and its blends against the interpolations.
CCMD(fua_copy_report)
{
	using namespace zx::gamecopy;
	Printf("copy before views: %d compared, %d differed\n%s", g_before.checked, g_before.differed, g_before.firsts.c_str());
	Printf("copy blends: %d compared, %d differed\n%s", g_after.checked, g_after.differed, g_after.firsts.c_str());
	zx::frame::ComputeOracleReset(g_before);
	zx::frame::ComputeOracleReset(g_after);
}
