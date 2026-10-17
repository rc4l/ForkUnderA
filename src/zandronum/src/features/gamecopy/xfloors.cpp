// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/gamecopy/xfloors.h"
#include "features/gamecopy/computation/memberindex_compute.h"

#include "p_3dfloors.h"
#include "r_state.h"
#include "v_palette.h"

#include <cstring>
#include <memory>
#include <vector>

namespace zx { namespace gamecopy {

namespace {

// [rc4l] A sector plane a copy follows: the sector and floor or ceiling, sector -1 for a plane that follows none.
struct PlaneSource { int sector = -1; int pos = 0; };

// [rc4l] A copied 3D floor with the planes its planerefs point at and what its colormap calls gave.
struct F3DFloorCopy : F3DFloor
{
	secplane_t topPlane, bottomPlane;
	PlaneSource topSource, bottomSource;
	FDynamicColormap *colormap;
	PalEntry blend;
};

// [rc4l] A sector's copy: the 3D floors it owns and the plane each light band follows.
struct SectorXFloors
{
	int sector;
	std::vector<std::unique_ptr<F3DFloorCopy>> owned;
	std::vector<PlaneSource> bands;
};

std::vector<SectorXFloors> g_sectors;

// [rc4l] The sector a pointer to the member `first` has in sector 0 points into, -1 for none.
int SectorOf(const void *member, const void *first)
{
	return numsectors > 0 ? ComputeMemberIndex(sectors, sizeof(sector_t), numsectors, size_t((const char *)first - (const char *)&sectors[0]), member) : -1;
}

// [rc4l] The sector plane a live plane pointer points at.
PlaneSource SourceOf(const secplane_t *plane)
{
	PlaneSource p;
	if (numsectors <= 0) return p;
	if ((p.sector = SectorOf(plane, &sectors[0].floorplane)) >= 0) p.pos = sector_t::floor;
	else if ((p.sector = SectorOf(plane, &sectors[0].ceilingplane)) >= 0) p.pos = sector_t::ceiling;
	return p;
}

// [rc4l] A pointer the game keeps into a live sector's member, pointed at the same member of its copy.
const FTextureID *CopyTexture(const FTextureID *live)
{
	for (int pos = 0; pos < 2 && numsectors > 0; pos++)
	{
		const int s = SectorOf(live, &sectors[0].planes[pos].Texture);
		if (s >= 0) return &sectors[s].rc.planes[pos].Texture;
	}
	return live;
}
const fixed_t *CopyTexZ(const fixed_t *live)
{
	for (int pos = 0; pos < 2 && numsectors > 0; pos++)
	{
		const int s = SectorOf(live, &sectors[0].planes[pos].TexZ);
		if (s >= 0) return &sectors[s].rc.planes[pos].TexZ;
	}
	return live;
}
short *CopyLight(short *live)
{
	const int s = live != NULL && numsectors > 0 ? SectorOf(live, &sectors[0].lightlevel) : -1;
	return s >= 0 ? &sectors[s].rc.lightlevel : live;
}

// [rc4l] Moves a copied plane to its source's copy; whether it moved.
bool Follow(secplane_t &plane, const PlaneSource &source)
{
	if (source.sector < 0) return false;
	const secplane_core_t &to = sectors[source.sector].rc.GetSecPlane(source.pos);
	if (plane == to && plane.ic == to.ic) return false;
	static_cast<secplane_core_t &>(plane) = to;
	return true;
}

// [rc4l] A planeref pointed at the copy: its plane the copy's own, its texture and height the model's copy.
void CopyPlaneRef(F3DFloor::planeref &out, const F3DFloor::planeref &live, secplane_t &plane, PlaneSource &source)
{
	out = live;
	source = SourceOf(live.plane);
	memset(&plane, 0, sizeof(plane));
	static_cast<secplane_core_t &>(plane) = *live.plane;
	Follow(plane, source);
	out.plane = &plane;
	out.texture = CopyTexture(live.texture);
	out.texheight = CopyTexZ(live.texheight);
}

// [rc4l] The copy of a live 3D floor in a sector's live list, by its place there, NULL for one not in it.
F3DFloor *CopyOf(const extsector_t::xfloor &live, const SectorXFloors &x, const F3DFloor *rover)
{
	if (rover == NULL) return NULL;
	for (unsigned k = 0; k < live.ffloors.Size() && k < x.owned.size(); k++) if (live.ffloors[k] == rover) return x.owned[k].get();
	return NULL;
}

// [rc4l] The plane a light band was copied from: its caster's top or bottom, else the sector's ceiling, compared by value at the fill.
PlaneSource BandSource(const lightlist_t &band, const sector_t &sector)
{
	if (band.caster != NULL)
	{
		if (band.plane == *band.caster->top.plane) return SourceOf(band.caster->top.plane);
		if (band.plane == *band.caster->bottom.plane) return SourceOf(band.caster->bottom.plane);
	}
	if (band.plane == sector.ceilingplane) return SourceOf(&sector.ceilingplane);
	return PlaneSource();
}

bool SameRover(const F3DFloorCopy &a, const F3DFloorCopy &b)
{
	return a.flags == b.flags && a.alpha == b.alpha && a.model == b.model && a.target == b.target && a.master == b.master &&
		a.colormap == b.colormap && a.blend.d == b.blend.d && a.top.texture == b.top.texture && a.bottom.texture == b.bottom.texture &&
		a.topSource.sector == b.topSource.sector && a.topSource.pos == b.topSource.pos && a.bottomSource.sector == b.bottomSource.sector &&
		a.bottomSource.pos == b.bottomSource.pos && static_cast<const secplane_core_t &>(a.topPlane) == b.topPlane && static_cast<const secplane_core_t &>(a.bottomPlane) == b.bottomPlane;
}

bool SameBand(const lightlist_t &a, const lightlist_t &b)
{
	return a.plane == b.plane && a.p_lightlevel == b.p_lightlevel && a.extra_colormap == b.extra_colormap && a.blend.d == b.blend.d &&
		a.flags == b.flags && a.lightsource == b.lightsource && a.caster == b.caster && a.fromsector == b.fromsector;
}

// [rc4l] Copies one sector's 3D floors and light list, reusing last fill's copies; whether any of it changed.
bool FillSector(SectorXFloors &x)
{
	sector_t &s = sectors[x.sector];
	const extsector_t::xfloor &live = s.e->XFloor;
	extsector_t::xfloorcopy &copy = s.e->rcXFloor;
	bool changed = copy.ffloors.Size() != live.ffloors.Size() || copy.lightlist.Size() != live.lightlist.Size();
	while (x.owned.size() < live.ffloors.Size()) x.owned.emplace_back(new F3DFloorCopy());
	copy.ffloors.Resize(live.ffloors.Size());
	for (unsigned k = 0; k < live.ffloors.Size(); k++)
	{
		F3DFloorCopy &c = *x.owned[k];
		const F3DFloorCopy before = c;
		F3DFloor &rover = *live.ffloors[k];
		static_cast<F3DFloor &>(c) = rover;
		CopyPlaneRef(c.top, rover.top, c.topPlane, c.topSource);
		CopyPlaneRef(c.bottom, rover.bottom, c.bottomPlane, c.bottomSource);
		c.toplightlevel = CopyLight(rover.toplightlevel);
		c.colormap = rover.GetColormap();
		c.blend = rover.GetBlend();
		changed |= !SameRover(before, c);
		copy.ffloors[k] = &c;
	}
	copy.lightlist.Resize(live.lightlist.Size());
	x.bands.resize(live.lightlist.Size());
	for (unsigned i = 0; i < live.lightlist.Size(); i++)
	{
		const lightlist_t &band = live.lightlist[i];
		const lightlist_t before = copy.lightlist[i];
		lightlist_t &c = copy.lightlist[i];
		c = band;
		c.p_lightlevel = CopyLight(band.p_lightlevel);
		c.lightsource = CopyOf(live, x, band.lightsource);
		c.caster = CopyOf(live, x, band.caster);
		x.bands[i] = BandSource(band, s);
		changed |= !SameBand(before, c);
	}
	return changed;
}

} // namespace

// [rc4l] The last level's sectors are gone by now, so their copies are only dropped, never touched.
void InitXFloors()
{
	g_sectors.clear();
	for (int i = 0; i < numsectors; i++)
	{
		if (sectors[i].e == NULL) continue;
		sectors[i].e->rcXFloor.ffloors.Clear();
		sectors[i].e->rcXFloor.lightlist.Clear();
		if (sectors[i].e->XFloor.ffloors.Size() == 0 && sectors[i].e->XFloor.lightlist.Size() == 0) continue;
		g_sectors.emplace_back();
		g_sectors.back().sector = i;
	}
	FillXFloors();
}

void FillXFloors()
{
	for (SectorXFloors &x : g_sectors)
		if (FillSector(x)) sectors[x.sector].rcShape++;
}

void BlendXFloors()
{
	for (SectorXFloors &x : g_sectors)
	{
		extsector_t::xfloorcopy &copy = sectors[x.sector].e->rcXFloor;
		bool moved = false;
		for (unsigned k = 0; k < copy.ffloors.Size(); k++)
		{
			F3DFloorCopy &c = *x.owned[k];
			moved |= Follow(c.topPlane, c.topSource);
			moved |= Follow(c.bottomPlane, c.bottomSource);
		}
		for (unsigned i = 0; i < copy.lightlist.Size(); i++) moved |= Follow(copy.lightlist[i].plane, x.bands[i]);
		if (moved) sectors[x.sector].rcShape++;
	}
}

FDynamicColormap *XFloorColormap(const F3DFloor *copy)
{
	return static_cast<const F3DFloorCopy *>(copy)->colormap;
}

PalEntry XFloorBlend(const F3DFloor *copy)
{
	return static_cast<const F3DFloorCopy *>(copy)->blend;
}

sector_t *HeightSecOf(const sector_t *sector)
{
	if (sector->heightsec == NULL || (sector->heightsec->rc.MoreFlags & SECF_IGNOREHEIGHTSEC)) return NULL;
	if (sector->e != NULL)
		for (unsigned i = sector->e->rcXFloor.ffloors.Size(); i-- > 0; )
			if ((sector->e->rcXFloor.ffloors[i]->flags & (FF_EXISTS | FF_RENDERPLANES)) == (FF_EXISTS | FF_RENDERPLANES)) return NULL;
	return sector->heightsec;
}

lightlist_t *GetPlaneLight(sector_t *sector, const secplane_core_t &plane, bool underside)
{
	TArray<lightlist_t> &lightlist = sector->e->rcXFloor.lightlist;
	fixed_t planeheight = plane.ZatPoint(CenterSpot(sector));
	if (underside) planeheight--;
	for (unsigned i = 1; i < lightlist.Size(); i++)
		if (lightlist[i].plane.ZatPoint(CenterSpot(sector)) <= planeheight) return &lightlist[i - 1];
	return &lightlist[lightlist.Size() - 1];
}

}} // namespace zx::gamecopy
