// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <stddef.h>   // offsetof, for the cached-record guard
#include "features/levelmesh/wallcache.h"
#include "features/levelmesh/computation/wallcache_compute.h"   // [rc4l] SideLook, SectorLook
#include "features/levelmesh/captureshading.h"
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/fakeplanes.h"   // [rc4l] SideTexturesChanged, for the fills a switch can open or close
#include "features/levelmesh/computation/flatmesh_compute.h"

#include "r_defs.h"
#include "r_state.h"
#include "p_3dfloors.h"   // F3DFloor, for the 3D floor wall faces
#include "features/levelmesh/computation/lightband_compute.h"   // [rc4l] BandPlane, the band boundaries in map units
#include "doomdata.h"
#include "r_sky.h"   // skyflatnum, for the sky surfaces
#include "features/surfaces/walltypes.h"   // [rc4l] RENDERWALL_*
#include "features/levelmesh/computation/wallbatch_compute.h"   // [rc4l] ComputeFanTriangleVertex
#include "features/materials/materials.h"
#include "features/materials/computation/texcoord_compute.h"   // [rc4l] FTexCoordInfo
#include "features/hwrender/computation/walllight_compute.h"
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "c_dispatch.h"
#include "stats.h"   // cycle_t, to time the invalidation loop
#include "c_cvars.h"
#include "c_console.h"
#include "doomstat.h"   // [rc4l] gametic, which bounds how long a relight may wait

#include <chrono>

// [rc4l] Keep the level mesh the backend draws from, and CUSTOM so turning it over drops what the mesh held; the name is GL's, from when GL's walk filled it.
CUSTOM_CVAR(Bool, gl_wallmesh, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
{
	zx::levelmesh::InvalidateAll();
}
EXTERN_CVAR(Bool, gl_mirrors)
// [rc4l] Cut a wall at its 3D floor light bands (wallbands_compute), a switch so the banding can be A/B'd.
CVAR(Bool, fua_surface_bands, true, 0)
// [rc4l] Print each banded wall the map bake splits: its span, the first boundary and the band that lights its top.
CVAR(Bool, fua_band_log, false, 0)
EXTERN_CVAR(Int, gl_fogmode)
// [rc4l] Off bakes a scrolled wall or floor again instead of shifting its texture, the old cost, for an A/B in one session.
CVAR(Bool, fua_mesh_rescroll, true, 0)
// [rc4l] Microseconds a frame spends re-lighting sectors whose light alone changed before leaving the rest to the next frames, none past the next tic; 0 re-lights every one at once.
CVAR(Int, fua_relight_budget_us, 80, CVAR_ARCHIVE)

#include "features/surfaces/surfacebuild.h"
#include "features/surfaces/computation/wallbands_compute.h"
#include "features/surfaces/computation/ffblocks_compute.h"
#include "features/horizon/computation/horizon_compute.h"

namespace zx { namespace levelmesh {

static TArray<SegCache> g_cache;
// [rc4l] Per-sector fua_dirty as of the last invalidation sweep, and the segs each sector owns.
//
// See InvalidateMovedSectors: a moving sector's geometry has to be dropped from the mesh whether or
// not the BSP happens to walk it, and answering "which segs belong to this sector" by scanning all
// segs every frame would be silly when the mapping never changes.
static TArray<int>          g_sectorDirty;
// [rc4l] Per-sidedef fua_dirty as of the last sweep, so a switch's new texture reaches the map bake.
static TArray<int>          g_sideDirty;
// [rc4l] Per-sector fua_placement as of the last sweep, so a scrolled floor reaches the map bake.
static TArray<int>          g_sectorPlacement;
static TArray<TArray<int> > g_sectorSegs;
// [rc4l] Bumped once per level load. A backend that wants to set itself up per level needs to know a
// level happened, and "numsegs changed" is not that -- reloading the same map gives the same count.
static int              g_levelGeneration = 0;

int LevelGeneration() { return g_levelGeneration; }

void AllocForLevel(int numsegs)
{
	g_levelGeneration++;
	// [rc4l] The flats and sprites belong to the level too, and used to outlive it.
	//
	// AllocForLevel resized the WALL cache and left the flat table alone, so after a map change
	// every flat key still named a subsector index from the previous level and still owned a mesh
	// range in an arena that MeshInitForLevel was about to wipe. Anything that read a cached flat
	// afterwards -- the standalone Vulkan frame, or fua_surface_verify -- was reading the old
	// level through pointers the new one had already reused, which took the process down with
	// nothing in the log.
	//
	// Cleared HERE and not in MeshInitForLevel because ClearFlats gives its ranges back to the
	// mesh, and this runs while that mesh is still the one that handed them out.
	// The per-sidedef bake owners belong to the level that is going away.
	extern void ClearSideOwners();
	ClearSideOwners();
	zx::levelmesh::ClearFlats();
	zx::levelmesh::ClearSprites();
	g_cache.Clear();
	if (numsegs <= 0) return;
	g_cache.Resize(numsegs);
	g_sectorDirty.Clear();
	g_sectorPlacement.Clear();
	g_sectorSegs.Clear();
	g_sideDirty.Clear();
	if (numsides > 0)
	{
		g_sideDirty.Resize(numsides);
		for (int i = 0; i < numsides; i++) g_sideDirty[i] = sides[i].fua_dirty;
	}
	if (numsectors > 0)
	{
		g_sectorDirty.Resize(numsectors);
		g_sectorPlacement.Resize(numsectors);
		g_sectorSegs.Resize(numsectors);
		for (int i = 0; i < numsectors; i++)
		{
			g_sectorDirty[i] = sectors[i].fua_dirty;
			g_sectorPlacement[i] = sectors[i].fua_placement;
			g_sectorSegs[i].Clear();
		}
		for (int i = 0; i < numsegs; i++)
		{
			if (segs[i].frontsector != NULL)
				g_sectorSegs[int(segs[i].frontsector - sectors)].Push(i);
			if (segs[i].backsector != NULL && segs[i].backsector != segs[i].frontsector)
				g_sectorSegs[int(segs[i].backsector - sectors)].Push(i);
		}
	}

	for (int i = 0; i < numsegs; i++)
	{
		// Belt and braces with SegCache's constructor: Resize is not guaranteed to construct a
		// recycled slot, and a stale range here corrupts the new level's mesh rather than merely
		// wasting a slot.
		g_cache[i].pieceCount = 0;
		g_cache[i].bakedCount = 0;
		g_cache[i].mapBaked = false;
		for (int k = 0; k < kMaxMapPieces; k++)
		{
			g_cache[i].pieces[k].list = 0;
			g_cache[i].pieces[k].range.offset = 0;
			g_cache[i].pieces[k].range.count = 0;
		}
	}
}

void FreeLevel()
{
	g_cache.Clear();
	ForgetWalks();
}

// [rc4l] A wall piece into the mesh, filed under the sidedef it was baked from, since working it out again from where the piece lies picks a neighbour on a narrow wall (WDI14's painting wall).
static void RegisterSegPiece(MeshPiece &mp, const seg_t *seg)
{
	if (seg != NULL && seg->sidedef != NULL) mp.linkId = numsubsectors + int(seg->sidedef - sides);
	MeshRegisterPiece(mp);
}


// [rc4l] One quad per sidedef, not per seg: the derivation draws the whole linedef as GLWall::Process does, so baking each seg of a split line would stack identical coplanar quads.
static TArray<int> g_sideOwner;      // sidedef index -> the one seg that bakes it
// [rc4l] Each sidedef as its owner seg last baked it, so a scroller's offset can move the pieces already baked.
static TArray<zx::levelmesh::SideLook> g_sideBaked;
static TArray<bool> g_sideBakedValid;

static void BuildSideOwners()
{
	g_sideOwner.Clear();
	if (sides == NULL || numsides <= 0 || segs == NULL) return;
	g_sideOwner.Reserve(numsides);
	for (int i = 0; i < numsides; i++) g_sideOwner[i] = -1;
	for (int i = 0; i < numsegs; i++)
	{
		if (segs[i].sidedef == NULL) continue;
		const int sd = (int)(segs[i].sidedef - sides);
		if (sd < 0 || sd >= numsides) continue;
		if (g_sideOwner[sd] < 0) g_sideOwner[sd] = i;
	}
}

void ClearSideOwners() { g_sideOwner.Clear(); }

int SideOwnerSeg(int side)
{
	if (sides == NULL || side < 0 || side >= numsides) return -1;
	if (g_sideOwner.Size() != (unsigned)numsides) BuildSideOwners();
	return (unsigned)side < g_sideOwner.Size() ? g_sideOwner[side] : -1;
}

bool DerivedPart(const seg_t *seg, int part, zx::surfaces::DerivedWallSpan &d, bool &twoSidedMiddle)
{
	const int type = ComputePartRenderType(part, seg->backsector != NULL);
	twoSidedMiddle = type == RENDERWALL_M2S;
	return type != RENDERWALL_NONE && zx::surfaces::BuildDerivedWallSpan(seg, type, d);
}

MidTextureBlend LineMidBlend(const line_t *line, zx::materials::MaterialRef mat)
{
	return ComputeMidTextureBlend(FIXED2FLOAT(line->Alpha), (line->flags & ML_ADDTRANS) != 0, zx::materials::MaterialTransparent(mat));
}

bool LineDrawsNoWall(const seg_t *seg)
{
	return zx::horizon::ComputeIsHorizonLine(seg->linedef->special) ||
		ComputeIsMirrorWall(seg->linedef->special, seg->backsector == NULL, gl_mirrors);
}

static bool SegOwnsItsSide(int segIndex)
{
	if (segs == NULL || segIndex < 0 || segIndex >= numsegs) return false;
	const side_t *sd = segs[segIndex].sidedef;
	if (sd == NULL) return false;
	// Rebuilt when the table does not match the level's sidedef count, which is what a level change
	// looks like from here. (An earlier version stamped this with level.totaltime -- which changes
	// every tic, so it rebuilt the whole table on every seg of every bake.)
	if (g_sideOwner.Size() != (unsigned)numsides) BuildSideOwners();
	const int idx = (int)(sd - sides);
	if (idx < 0 || (unsigned)idx >= g_sideOwner.Size()) return false;
	return g_sideOwner[idx] == segIndex;
}


// [rc4l] Where the 3D floor faces live, above the parts. A sidedef with more slabs than this keeps
// as many as fit, which is a missing face on one wall rather than a wrong one everywhere.
static const int kFFSlot0 = 3 * kBandsPerPart;
static const int kMaxFFBlocks = kMaxMapPieces - kFFSlot0;

// The rovers that produced each block, so the emitter can read their flags back.
static F3DFloor *g_ffRoverOf[16];

// [rc4l] An engine plane in map units for the pure band helpers; reading one through FLOAT2FIXED's ints once took the double ZatPoint and lost a factor of 65536.
static zx::levelmesh::BandPlane ToBandPlane(const secplane_t &p)
{
	return zx::levelmesh::ComputeBandPlaneFromFixed((double)p.a, (double)p.b, (double)p.ic, (double)p.d);
}

// [rc4l] Where each light band ends at a wall's two ends: band i is bounded below by light i+1.
static void BandBottoms(const TArray<lightlist_t> &lights, int nLights, float x1, float y1, float x2, float y2,
	float out[][2])
{
	for (int i = 0; i + 1 < nLights; i++)
		zx::levelmesh::ComputeBandBottomAtEnds(ToBandPlane(lights[i + 1].plane), x1, y1, x2, y2, out[i]);
}

// [rc4l] The light list seg's bands are lit from and how many of its entries they take, at most kBandsPerPart.
static int BandLights(const seg_t *seg, const TArray<lightlist_t> *&lights)
{
	const sector_t *front = seg->frontsector;
	lights = (front != NULL && front->e != NULL) ? &front->e->XFloor.lightlist : NULL;
	const int n = (fua_surface_bands && lights != NULL) ? (int)lights->Size() : 0;
	return n > kBandsPerPart ? kBandsPerPart : n;
}

// [rc4l] Put3DWall's rule for a band lit by `ll`: its level unless it points back at the wall's own sector, and then the wall's.
static int BandLightLevel(const lightlist_t &ll, const seg_t *seg, int wallLevel)
{
	return ll.p_lightlevel != &seg->sidedef->sector->lightlevel ? gl_ClampLight(*ll.p_lightlevel) : wallLevel;
}

int DerivedPartBands(const seg_t *seg, const zx::surfaces::DerivedWallSpan &d, const zx::surfaces::DerivedWallLight &dl, PartBand *out)
{
	const TArray<lightlist_t> *lights;
	const int nLights = BandLights(seg, lights);
	float bandBottom[kBandsPerPart][2];
	if (nLights > 1) BandBottoms(*lights, nLights, d.x1, d.y1, d.x2, d.y2, bandBottom);
	zx::surfaces::WallBand bands[kBandsPerPart];
	const int n = zx::surfaces::ComputeWallBands(d.ztop, d.zbottom, bandBottom, nLights, bands, kBandsPerPart);
	if (fua_band_log && nLights > 1)
		Printf("bands seg %d: wall %.1f..%.1f, %d lights, first boundary %.1f, %d bands, first lit by %d\n",
			int(seg - segs), d.ztop[0], d.zbottom[0], nLights, bandBottom[0][0], n, n > 0 ? bands[0].lightIndex : -1);
	for (int bi = 0; bi < n; bi++)
	{
		PartBand &b = out[bi];
		b.band = bands[bi];
		b.lightLevel = dl.lightLevel;
		b.lightFrom = NULL;
		b.lightEntry = -1;
		// [rc4l] Put3DWall's rule: the level is the band's unless it points back at the wall's own sector, and the colour is the band's whenever the piece is one.
		if (!bands[bi].ownLight && bands[bi].lightIndex < nLights)
		{
			const lightlist_t &ll = (*lights)[bands[bi].lightIndex];
			b.lightLevel = BandLightLevel(ll, seg, dl.lightLevel);
			b.lightFrom = ll.extra_colormap;
			b.lightEntry = bands[bi].lightIndex;
		}
	}
	return n;
}

// A 3D floor plane evaluated at the wall's two ends, the way GLWall::GetPlanePos does it.
static void PlaneAt(const F3DFloor::planeref &pr, float x0, float y0, float x1, float y1, float *out)
{
	const secplane_t *p = pr.plane;
	if (p->a | p->b)
	{
		out[0] = zx::levelmesh::ComputeBandPlaneZ(ToBandPlane(*p), x0, y0);
		out[1] = zx::levelmesh::ComputeBandPlaneZ(ToBandPlane(*p), x1, y1);
	}
	else if (pr.isceiling == sector_t::ceiling) out[0] = out[1] = FIXED2FLOAT(p->d);
	else                                        out[0] = out[1] = FIXED2FLOAT(-p->d);
}

// [rc4l] A slab as the block walk sees it along this span: where it sits at both ends, which way it draws sides, and its clip kind.
static void ToFFRover(F3DFloor *rover, float x0, float y0, float x1, float y1, zx::surfaces::FFRover &r)
{
	PlaneAt(rover->top, x0, y0, x1, y1, r.top);
	PlaneAt(rover->bottom, x0, y0, x1, y1, r.bottom);
	r.renderSides = (rover->flags & FF_RENDERSIDES) != 0;
	r.invertSides = (rover->flags & FF_INVERTSIDES) != 0;
	r.kind = rover->flags & (FF_SWIMMABLE | FF_TRANSLUCENT);
}


// Why a reshade handed the seg back to the full bake, so the fallback rate is explainable.
static int g_reshadeFF = 0, g_reshadeNoLight = 0, g_reshadeNotOwner = 0, g_reshadeNoPiece = 0;

// [rc4l] Re-light a seg WITHOUT re-deriving it. Returns pieces repainted, or -1 if it declined.
//
// The case this exists for is a sector whose light changed and whose planes did not -- three
// quarters of all invalidations on a live level, because Doom's light effects run every tic and
// sector_t bumps fua_dirty for a flicker exactly as it does for a lift. BakeSegFromMap answered
// those by working the wall out from the map again: spans, UVs, band splits, materials, six
// vertices per band, all to arrive at the same geometry it already had and one different light.
//
// If the planes did not move, the band structure cannot have changed -- band boundaries are 3D-floor
// plane heights. So the only thing to redo is each band's light and the CaptureShading that follows
// it, and the rest can be read back off the piece the last bake registered.
//
// It declines rather than guesses whenever it is not certain the geometry is untouched: a seg it
// never baked, one carrying 3D-floor pieces (whose light comes from an F3DFloor, not the band list),
// or one whose stored piece has gone. The caller falls back to the full bake, which is always right.
static int ReshadeSegFromMap(int segIndex)
{
	if (!gl_wallmesh) return -1;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return -1;
	if (segs == NULL || segIndex >= numsegs) return -1;
	const seg_t *seg = &segs[segIndex];
	if (seg->sidedef == NULL || seg->linedef == NULL || seg->frontsector == NULL) return -1;


	SegCache &sc = g_cache[segIndex];
	if (sc.hasOwnLitPiece) { g_reshadeFF++; return -1; }   // [rc4l] its light is not in bandLight

	zx::surfaces::DerivedWallLight dl;
	FDynamicColormap *cmFrom = NULL;
	// [rc4l] Owner first, since it is a table lookup and the light is not, and about 140 segs a tic on AOW03 are not owners.
	if (!SegOwnsItsSide(segIndex)) { g_reshadeNotOwner++; return -1; }
	if (!zx::surfaces::BuildDerivedWallLight(seg, dl, cmFrom) || cmFrom == NULL) { g_reshadeNoLight++; return -1; }

	const TArray<lightlist_t> *lights;
	const int nLights = BandLights(seg, lights);

	int done = 0;
	for (int slot = 0; slot < kMaxMapPieces; slot++)
	{
		const MeshRange &range = sc.pieces[slot].range;
		if (range.count == 0) continue;
		const zx::levelmesh::MeshPiece *had = zx::levelmesh::MeshFindPiece(range.offset);
		if (had == NULL || had->range.count == 0) { g_reshadeNoPiece++; return -1; }

		// The band's light, by the same rule the bake uses -- see the band loop.
		int lightLevel = dl.lightLevel;
		FColormap cm;
		cm = cmFrom;
		const int li = sc.bandLight[slot];
		if (li >= 0 && li < nLights)
		{
			const lightlist_t &ll = (*lights)[li];
			lightLevel = BandLightLevel(ll, seg, lightLevel);
			cm.CopyLightColor(ll.extra_colormap);
		}

		// Everything that is not light comes back off the registered piece, which is the point.
		zx::levelmesh::MeshPiece mp = *had;
		const void *savedBase = mp.baseTex;
		const float nx = mp.normX, ny = mp.normY, nz = mp.normZ;
		// A translucent 3D-floor slab carries its own alpha and blend mode, set after the bake's
		// CaptureShading. Re-running it would hand them back their opaque defaults and the slab would
		// turn solid, so they are preserved here exactly as the base texture and normal are.
		const float savedAlpha = mp.alpha;
		const int   savedBlend = mp.blendMode;
		zx::levelmesh::CaptureShading(lightLevel, dl.relLight + getExtraLight(), cm, mp);
		// AFTER CaptureShading, which clears baseTex and resets alpha and blend mode on the way past.
		mp.baseTex    = savedBase;
		mp.alpha      = savedAlpha;
		mp.blendMode  = savedBlend;
		mp.lightLevel = lightLevel;
		mp.lightColor = cm.LightColor.d;
		mp.fadeColor  = cm.FadeColor.d;
		mp.normX = nx; mp.normY = ny; mp.normZ = nz;
		RegisterSegPiece(mp, seg);
		done++;
	}
	return done;
}

// [rc4l] One flooded gap: the wall quad, textured per fragment from the back plane and lit by DrawFloodedPlane's rule (that sector's plane light and colormap, extra light only).
static bool BakeFloodPiece(MeshRange &range, const zx::surfaces::DerivedWallSpan &d)
{
	static MeshVertex tris[6];
	MeshVertex fan[4];
	fan[0].Set(d.x1, d.zbottom[0], d.y1, d.uLeft,  d.vBottom[0]);
	fan[1].Set(d.x1, d.ztop[0],    d.y1, d.uLeft,  d.vTop[0]);
	fan[2].Set(d.x2, d.ztop[1],    d.y2, d.uRight, d.vTop[1]);
	fan[3].Set(d.x2, d.zbottom[1], d.y2, d.uRight, d.vBottom[1]);
	int w = 0;
	for (int t = 0; t < 2; t++)
		for (int c = 0; c < 3; c++)
			tris[w++] = fan[ComputeFanTriangleVertex(4, t, c)];
	if (!MeshStore(range, tris, w)) { MeshSquash(range); return false; }

	const sector_t *sec = d.floodSector;
	const void *mat = d.material;
	FColormap cm;
	cm = sec->ColorMap;
	int lightLevel = d.floodCeiling ? sec->GetCeilingLight() : sec->GetFloorLight();
	if (lightLevel < 0) lightLevel = -lightLevel;   // [rc4l] DrawFloodedPlane takes abs() of it
	ApplyFullbright(mat, cm, lightLevel);
	MeshPiece mp;
	mp.range = range;
	mp.material = d.material;
	zx::levelmesh::CaptureShading(lightLevel, getExtraLight(), cm, mp, false);
	// [rc4l] After CaptureShading, which resets what it does not decide.
	mp.baseTex = d.baseTex;
	mp.lightLevel = lightLevel;
	mp.lightColor = cm.LightColor.d;
	mp.fadeColor = cm.FadeColor.d;
	mp.flood = true;
	mp.floodZ = d.floodZ;
	for (int i = 0; i < 6; i++) mp.floodUV[i] = d.floodUV[i];
	MeshRegisterPiece(mp);
	return true;
}

static int g_sidesRescrolled = 0;

// [rc4l] A sidedef's parts (top, mid, bottom) as SideLook keeps them.
static zx::levelmesh::SideLook SideLookOf(const side_t &sd)
{
	zx::levelmesh::SideLook l;
	for (int p = 0; p < 3; p++)
	{
		l.tex[p] = sd.GetTexture(p).GetIndex();
		l.xoff[p] = sd.GetTextureXOffset(p).Raw();
		l.yoff[p] = sd.GetTextureYOffset(p).Raw();
		l.xscale[p] = sd.GetTextureXScale(p).Raw();
		l.yscale[p] = sd.GetTextureYScale(p).Raw();
	}
	l.light = sd.Light;
	l.flags = sd.Flags;
	return l;
}

// [rc4l] The look a sidedef's walls were just baked with, so a later scroll is measured from it.
static void RecordSideBaked(int sideIndex)
{
	if (sides == NULL || sideIndex < 0 || sideIndex >= numsides) return;
	if (g_sideBaked.Size() != (unsigned)numsides)
	{
		g_sideBaked.Resize((unsigned)numsides);
		g_sideBakedValid.Resize((unsigned)numsides);
		for (int i = 0; i < numsides; i++) g_sideBakedValid[i] = false;
	}
	g_sideBaked[sideIndex] = SideLookOf(sides[sideIndex]);
	g_sideBakedValid[sideIndex] = true;
}

// [rc4l] Shift the pieces a sidedef's owner seg baked by how far its offsets moved since, true when that was all it needed; anything else (a gap fill, a 3D floor's light, a two-sided middle moving up or down) is baked again.
static bool RescrollSide(int sideIndex)
{
	if (!fua_mesh_rescroll || (unsigned)sideIndex >= g_sideBakedValid.Size() || !g_sideBakedValid[sideIndex]) return false;
	if ((unsigned)sideIndex >= g_sideOwner.Size()) return false;
	const int segIndex = g_sideOwner[sideIndex];
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return false;
	SegCache &sc = g_cache[segIndex];
	if (!sc.mapBaked || sc.hasOwnLitPiece) return false;
	const side_t &sd = sides[sideIndex];
	const zx::levelmesh::SideLook now = SideLookOf(sd);
	const zx::levelmesh::SideLook &baked = g_sideBaked[sideIndex];
	// [rc4l] Only a pure offset change scrolls in place; anything else, and a dirty side that looks unchanged, takes the bake it always took.
	if (zx::levelmesh::ComputeSideChange(baked, now) != zx::levelmesh::kSideScrolled) return false;

	// [rc4l] The parts in BakeSegFromMap's slot order, and the sidedef part each one draws.
	static const int kPartTexpos[3] = { side_t::top, side_t::bottom, side_t::mid };
	float du[3] = { 0.f, 0.f, 0.f }, dv[3] = { 0.f, 0.f, 0.f };
	for (int part = 0; part < 3; part++)
	{
		const int tp = kPartTexpos[part];
		const void *base = TexMan.ByIndex(sd.GetTexture(tp).GetIndex());
		for (int k = 0; k < kBandsPerPart; k++)
		{
			const MeshRange &r = sc.pieces[part * kBandsPerPart + k].range;
			if (r.count == 0) continue;
			const MeshPiece *p = MeshFindPiece(r.offset);
			if (p == NULL || p->baseTex != base) return false;   // [rc4l] a gap filled with another texture
		}
		if (baked.xoff[tp] == now.xoff[tp] && baked.yoff[tp] == now.yoff[tp]) continue;
		const void *mat = zx::materials::MaterialForTextureId(sd.GetTexture(tp));
		if (mat == NULL) return false;
		FTexCoordInfo tci;
		zx::materials::MaterialTexCoordInfo(mat, &tci, sd.GetTextureXScale(tp), sd.GetTextureYScale(tp));
		if (tci.mRenderHeight == 0) return false;
		du[part] = tci.FloatToTexU(FIXED2FLOAT(tci.TextureOffset(fixed_t::FromRaw(now.xoff[tp]))))
			- tci.FloatToTexU(FIXED2FLOAT(tci.TextureOffset(fixed_t::FromRaw(baked.xoff[tp]))));
		dv[part] = (FIXED2FLOAT(tci.RowOffset(fixed_t::FromRaw(now.yoff[tp])))
			- FIXED2FLOAT(tci.RowOffset(fixed_t::FromRaw(baked.yoff[tp])))) / (float)tci.mRenderHeight;
		// [rc4l] A two-sided middle is cut to its texture's height, so moving it up or down moves its quad.
		if (part == 2 && segs[segIndex].backsector != NULL && dv[part] != 0.f) return false;
		if (!zx::levelmesh::ComputeScrollInRange(du[part], dv[part])) return false;
	}
	for (int part = 0; part < 3; part++)
		for (int k = 0; k < kBandsPerPart; k++)
		{
			const MeshRange &r = sc.pieces[part * kBandsPerPart + k].range;
			if (r.count == 0) continue;
			MeshPiece moved = *MeshFindPiece(r.offset);
			moved.scrollU = du[part];
			moved.scrollV = dv[part];
			MeshRegisterPiece(moved);
		}
	return true;
}

// [rc4l] Bake a seg from the map with no GLWall, each part and band in a fixed slot so a re-bake reuses the same mesh range.
int BakeSegFromMap(int segIndex)
{
	// [rc4l] DECLINED (-1) and "took it, made nothing" (0) differ: a declined seg is squashed by its caller, while one taken has already squashed every slot it did not fill, and squashing it again re-dirtied ranges just written, every frame.
	if (!gl_wallmesh) return -1;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return -1;
	if (segs == NULL || segIndex >= numsegs) return -1;
	const seg_t *seg = &segs[segIndex];
	if (seg->sidedef == NULL || seg->linedef == NULL || seg->frontsector == NULL) return -1;

	SegCache &sc = g_cache[segIndex];
	static MeshVertex tris[6];

	zx::surfaces::DerivedWallLight dl;
	FDynamicColormap *cmFrom = NULL;
	// One quad per sidedef -- see SegOwnsItsSide. Every other seg on it would build the same four
	// corners again, in the same plane, and the two would stipple against each other.
	if (!SegOwnsItsSide(segIndex)) return -1;
	// [rc4l] A seg whose light cannot be derived keeps its ranges as they are; squashing them here is what put a hole in dbab01.
	if (!zx::surfaces::BuildDerivedWallLight(seg, dl, cmFrom) || cmFrom == NULL) return -1;
	sc.mapBaked = true;
	RecordSideBaked(int(seg->sidedef - sides));

	// [rc4l] A horizon line or a mirror is taken and left empty, since GL draws no wall on either -- see features/horizon and RenderMirrors.
	if (LineDrawsNoWall(seg))
	{
		for (int k = 0; k < kMaxMapPieces; k++) MeshSquash(sc.pieces[k].range);
		return 0;
	}

	// [rc4l] The light bands the sector's 3D floors cast on every wall in it.
	//
	// A sector with 3D floors has no single light level: each floor contributes a band, and a wall
	// crossing one is drawn as two pieces with two lights. Computed per part because the span being
	// cut is the part's -- see wallbands_compute for the rule, which is GLWall::SplitWall's.
	sc.hasOwnLitPiece = false;   // [rc4l] set below; a fog boundary or a flood is a piece a reshade cannot redo
	const sector_t *front = seg->frontsector;
	const TArray<lightlist_t> *lights =
		(front != NULL && front->e != NULL) ? &front->e->XFloor.lightlist : NULL;
	int nLights = (fua_surface_bands && lights != NULL) ? (int)lights->Size() : 0;
	if (nLights > kBandsPerPart) nLights = kBandsPerPart;

	int made = 0;
	for (int part = 0; part < kWallParts; part++)
	{
		const int slot0 = part * kBandsPerPart;
		const int partType = ComputePartRenderType(part, seg->backsector != NULL);

		zx::surfaces::DerivedWallSpan d;
		if (!zx::surfaces::BuildDerivedWallSpan(seg, partType, d))
		{
			// [rc4l] Nothing here now -- SQUASH it, do not give the range back.
			//
			// A part that comes and goes is the normal case: a door's upper exists when it is shut
			// and not when it is open, several times a second. Retiring the range means the next
			// re-bake allocates a fresh one at a different offset, which is a rebatch, which is a
			// full scene rebuild -- 4869 of them on dbab04 before this line said squash. Squashing
			// keeps the range so the geometry can come back into it, which is exactly the reasoning
			// InvalidateMovedSectors gives for doing the same thing.
			for (int k = 0; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
			continue;
		}

		// [rc4l] A flooded gap is one piece lit like the plane it shows, as DrawFloodedPlane lights it, so it takes no bands.
		if (d.flood)
		{
			if (BakeFloodPiece(sc.pieces[slot0].range, d)) { sc.hasOwnLitPiece = true; made++; }
			for (int k = 1; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
			continue;
		}

		PartBand bands[kBandsPerPart];
		const int n = DerivedPartBands(seg, d, dl, bands);
		if (n <= 0)
		{
			for (int k = 0; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
			continue;
		}

		const float spanTop0 = d.ztop[0], spanBottom0 = d.zbottom[0];
		const float spanTop1 = d.ztop[1], spanBottom1 = d.zbottom[1];
		const float h0 = spanTop0 - spanBottom0, h1 = spanTop1 - spanBottom1;
		for (int bi = 0; bi < n; bi++)
		{
			const zx::surfaces::WallBand &band = bands[bi].band;
			MeshRange &range = sc.pieces[slot0 + bi].range;

			// v follows z linearly across the whole wall, so a band takes its own v off the parent's
			// pair rather than recomputing it from the texture -- the same interpolation SplitWall
			// does when it cuts a copy of itself.
			const float vt0 = (h0 != 0.f) ? d.vTop[0] + (spanTop0 - band.ztop[0])    * (d.vBottom[0] - d.vTop[0]) / h0 : d.vTop[0];
			const float vb0 = (h0 != 0.f) ? d.vTop[0] + (spanTop0 - band.zbottom[0]) * (d.vBottom[0] - d.vTop[0]) / h0 : d.vBottom[0];
			const float vt1 = (h1 != 0.f) ? d.vTop[1] + (spanTop1 - band.ztop[1])    * (d.vBottom[1] - d.vTop[1]) / h1 : d.vTop[1];
			const float vb1 = (h1 != 0.f) ? d.vTop[1] + (spanTop1 - band.zbottom[1]) * (d.vBottom[1] - d.vTop[1]) / h1 : d.vBottom[1];

			MeshVertex fan[4];
			fan[0].Set(d.x1, band.zbottom[0], d.y1, d.uLeft,  vb0);
			fan[1].Set(d.x1, band.ztop[0],    d.y1, d.uLeft,  vt0);
			fan[2].Set(d.x2, band.ztop[1],    d.y2, d.uRight, vt1);
			fan[3].Set(d.x2, band.zbottom[1], d.y2, d.uRight, vb1);
			int w = 0;
			for (int t = 0; t < 2; t++)
				for (int c = 0; c < 3; c++)
					tris[w++] = fan[ComputeFanTriangleVertex(4, t, c)];

			if (!MeshStore(range, tris, w)) { MeshSquash(range); continue; }

			MeshPiece mp;
			mp.range = range;
			mp.material = d.material;

			// [rc4l] The band's light, by GLWall::Put3DWall's rule.
			//
			// Only the light LEVEL is conditional -- a band whose level points at the seg's own
			// sector is a light-transfer effect and must not override it -- while the colormap is
			// copied whenever the piece went through Put3DWall at all. The uppermost piece does not:
			// SplitWall puts that one with PutWall, which alters neither.
			const int lightLevel = bands[bi].lightLevel;
			FColormap cm;
			cm = cmFrom;
			// Remembered so ReshadeSegFromMap can redo exactly this without re-deriving the wall.
			sc.bandLight[slot0 + bi] = (signed char)bands[bi].lightEntry;
			if (bands[bi].lightEntry >= 0) cm.CopyLightColor(bands[bi].lightFrom);

			zx::levelmesh::CaptureShading(lightLevel, dl.relLight + getExtraLight(), cm, mp);
			// [rc4l] AFTER CaptureShading, which ends by clearing baseTex.
			//
			// It sets the fields a surface has no opinion about -- blend mode, alpha, sort position,
			// base texture -- back to their defaults, on the assumption the caller fills in whatever
			// it actually knows afterwards. The capture path does exactly that and has always been
			// fine. This one set it BEFORE and had it wiped every time: 1800 of dbab02's map-baked
			// walls carried no base texture at all, which is the identity the animation pass
			// re-resolves a material from, so a map-driven nukage wall could never have flowed.
			mp.baseTex = d.baseTex;
			mp.lightLevel = lightLevel;
			mp.lightColor = cm.LightColor.d;
			mp.fadeColor = cm.FadeColor.d;
			// [rc4l] A two-sided middle blends as GL draws it: the linedef's alpha and style, or the texture's own see-through texels.
			if (partType == RENDERWALL_M2S)
			{
				const MidTextureBlend mb = LineMidBlend(seg->linedef, d.material);
				mp.alpha = mb.alpha;
				mp.blendMode = mb.blendMode;
			}
			// [rc4l] The wall's normal: perpendicular to its direction, in mesh space (x, z-up, y), on
			// the side the winding faces -- the same rule the capture path uses, so front faces agree.
			{
				const float dx = d.x2 - d.x1, dy = d.y2 - d.y1;
				const float len = sqrtf(dx*dx + dy*dy);
				if (len > 0.0001f) { mp.normX = dy / len; mp.normY = 0.f; mp.normZ = -dx / len; }
			}
			mp.plain = ComputeWallPlainPass(partType);
			RegisterSegPiece(mp, seg);
			made++;
		}
		// Bands this part no longer has: hold the range, do not give it back.
		for (int k = n; k < kBandsPerPart; k++) MeshSquash(sc.pieces[slot0 + k].range);
	}
	// [rc4l] ...and the wall faces the sector's 3D floors put on this sidedef.
	//
	// A 3D floor is a slab hanging in the sector on the OTHER side of this line, and each one cuts a
	// block out of the wall behind it. GL builds these during its walk (DoFFloorBlocks), so when the
	// walk stops they vanish -- on dbab02 that was every red rock panel and the whole of that map's
	// 4.0% against the GL-driven picture.
	//
	// The slabs come from the BACK sector, because a slab is drawn on the wall you see it against.
	// The walk itself -- clip each to what the last one left, stop at the wall's bottom -- is in
	// ffblocks_compute, where it is tested against the cases GL's loop encodes.
	if (seg->backsector != NULL && seg->backsector->e != NULL)
	{
		const TArray<F3DFloor *> &ff = seg->backsector->e->XFloor.ffloors;
		// [rc4l] The span to cut is the OPENING, not the middle texture's.
		//
		// Asking BuildDerivedWallSpan for the middle looked tidy and gated the whole thing on the
		// sidedef having a midtexture -- which most two-sided lines with a 3D floor behind them do
		// not, so 32 sectors' worth of slabs produced three faces. DoFFloorBlocks takes the gap
		// between the planes instead: the lower of the two ceilings and the higher of the two floors,
		// each decided at BOTH ends together.
		struct { float x1, y1, x2, y2; float ztop[2]; float zbottom[2]; } mid;
		bool haveSpan = false;
		if (ff.Size() != 0 && seg->frontsector != NULL)
		{
			const vertex_t *lv1 = seg->linedef->v1, *lv2 = seg->linedef->v2;
			if (seg->sidedef != seg->linedef->sidedef[0]) { lv1 = seg->linedef->v2; lv2 = seg->linedef->v1; }
			mid.x1 = FIXED2FLOAT(lv1->x); mid.y1 = FIXED2FLOAT(lv1->y);
			mid.x2 = FIXED2FLOAT(lv2->x); mid.y2 = FIXED2FLOAT(lv2->y);
			const sector_t *fs = seg->frontsector, *bs = seg->backsector;
			float fch[2], bch[2], ffh[2], bfh[2];
			fch[0] = FIXED2FLOAT(fs->ceilingplane.ZatPoint(lv1)); fch[1] = FIXED2FLOAT(fs->ceilingplane.ZatPoint(lv2));
			bch[0] = FIXED2FLOAT(bs->ceilingplane.ZatPoint(lv1)); bch[1] = FIXED2FLOAT(bs->ceilingplane.ZatPoint(lv2));
			ffh[0] = FIXED2FLOAT(fs->floorplane.ZatPoint(lv1));   ffh[1] = FIXED2FLOAT(fs->floorplane.ZatPoint(lv2));
			bfh[0] = FIXED2FLOAT(bs->floorplane.ZatPoint(lv1));   bfh[1] = FIXED2FLOAT(bs->floorplane.ZatPoint(lv2));
			if (fch[0] < bch[0] && fch[1] < bch[1]) { mid.ztop[0] = fch[0]; mid.ztop[1] = fch[1]; }
			else                                    { mid.ztop[0] = bch[0]; mid.ztop[1] = bch[1]; }
			if (ffh[0] > bfh[0] && ffh[1] > bfh[1]) { mid.zbottom[0] = ffh[0]; mid.zbottom[1] = ffh[1]; }
			else                                    { mid.zbottom[0] = bfh[0]; mid.zbottom[1] = bfh[1]; }
			haveSpan = true;
		}
		if (haveSpan)
		{
			// [rc4l] Where the front sector's light bands end along THIS span.
			//
			// BuildFFBlock finishes with SplitWall, so a 3D floor's face is cut by the front
			// sector's light list exactly as an ordinary wall part is. Skipping that is what turned
			// dbab04's blue light strip grey: the geometry and the texture were already right and
			// only the colormap was the sector's instead of the band's.
			float ffBandBottom[kBandsPerPart][2];
			if (nLights > 1) BandBottoms(*lights, nLights, mid.x1, mid.y1, mid.x2, mid.y2, ffBandBottom);

			zx::surfaces::FFRover rovers[kMaxFFBlocks];
			int nr = 0;
			for (unsigned k = 0; k < ff.Size() && nr < kMaxFFBlocks; k++)
			{
				F3DFloor *rover = ff[k];
				if (rover == NULL || !(rover->flags & FF_EXISTS)) continue;
				ToFFRover(rover, mid.x1, mid.y1, mid.x2, mid.y2, rovers[nr]);
				// [rc4l] A fog slab DOES draw -- it just draws no texture.
				//
				// This used to skip them, reasoning that an untextured face is nothing for the mesh
				// to hold. It is not nothing: BuildFFBlock turns a fog slab into a flat translucent
				// panel of the light's fade colour, laid over the wall behind it, and the wall shows
				// through tinted. On dbab04 that is a blue water band, and skipping it left the band
				// grey with its geometry and its texture both already correct -- a colour-only fault,
				// which is the kind that survives every check that compares shapes.
				//
				// The capture path already registers such a piece with a NULL material, so the mesh
				// and the backend both handle one; there was nothing to add but the decision.
				g_ffRoverOf[nr] = rover;
				nr++;
			}

			// [rc4l] This side's own slabs, which take away the water a neighbour's water would wall off, as GL's ClipFFloors does.
			zx::surfaces::FFRover nearRovers[kMaxFFBlocks];
			int nn = 0;
			const sector_t *fs = seg->frontsector;
			for (unsigned k = 0; fs->e != NULL && k < fs->e->XFloor.ffloors.Size() && nn < kMaxFFBlocks; k++)
			{
				F3DFloor *rover = fs->e->XFloor.ffloors[k];
				if (rover != NULL && (rover->flags & FF_EXISTS))
					ToFFRover(rover, mid.x1, mid.y1, mid.x2, mid.y2, nearRovers[nn++]);
			}

			zx::surfaces::FFBlock blocks[kMaxFFBlocks];
			const int nb = zx::surfaces::ComputeFFBlocks(mid.ztop, mid.zbottom, rovers, nr,
				blocks, kMaxFFBlocks, nearRovers, nn);

			for (int bi = 0; bi < nb; bi++)
			{
				const zx::surfaces::FFBlock &blk = blocks[bi];
				F3DFloor *rover = g_ffRoverOf[blk.rover];
				MeshRange &range = sc.pieces[kFFSlot0 + bi].range;

				// [rc4l] Which texture, by BuildFFBlock's rule: the sidedef's upper or lower when the
				// slab asks for one, and otherwise the MASTER sidedef's middle -- the line that
				// defines the 3D floor, not the one being drawn on.
				const bool isFog = (rover->flags & FF_FOG) != 0;
				if (isFog)
				{
					// The fog plane defines the light, not the front sector -- GL's own comment.
					lightlist_t *fl = (rover->target != NULL && rover->top.plane != NULL) ?
						P_GetPlaneLight(rover->target, rover->top.plane, true) : NULL;
					if (fl == NULL || fl->extra_colormap == NULL) { MeshSquash(range); continue; }

					MeshVertex ffan[4];
					ffan[0].Set(mid.x1, blk.bottom[0], mid.y1, 0.f, 0.f);
					ffan[1].Set(mid.x1, blk.top[0],    mid.y1, 0.f, 0.f);
					ffan[2].Set(mid.x2, blk.top[1],    mid.y2, 0.f, 0.f);
					ffan[3].Set(mid.x2, blk.bottom[1], mid.y2, 0.f, 0.f);
					int fgw = 0;
					for (int t = 0; t < 2; t++)
						for (int cc = 0; cc < 3; cc++)
							tris[fgw++] = ffan[ComputeFanTriangleVertex(4, t, cc)];
					if (!MeshStore(range, tris, fgw)) { MeshSquash(range); continue; }

					MeshPiece fmp;
					fmp.range = range;
					fmp.material = NULL;   // no texture: the panel is its colour and nothing else
					FColormap fcm;
					fcm.Clear();
					fcm.LightColor = fl->extra_colormap->Fade;
					zx::levelmesh::CaptureShading(gl_ClampLight(*fl->p_lightlevel), 0, fcm, fmp, false);
					fmp.baseTex = NULL;
					fmp.lightLevel = gl_ClampLight(*fl->p_lightlevel);
					fmp.lightColor = fcm.LightColor.d;
					fmp.fadeColor = fcm.FadeColor.d;
					fmp.alpha = rover->alpha / 255.0f;
					fmp.blendMode = ComputeSurfaceBlendMode((rover->flags & FF_ADDITIVETRANS) != 0,
						fmp.alpha);
					{
						const float dx = mid.x2 - mid.x1, dy = mid.y2 - mid.y1;
						const float len = sqrtf(dx*dx + dy*dy);
						if (len > 0.0001f) { fmp.normX = dy / len; fmp.normY = 0.f; fmp.normZ = -dx / len; }
					}
					RegisterSegPiece(fmp, seg);
					// A fog boundary takes its light straight off the F3DFloor rather than from the
					// band list, so it is the one piece bandLight cannot describe -- see Reshade.
					sc.hasOwnLitPiece = true;
					made++;
					continue;
				}

				side_t *mastersd = (rover->master != NULL) ? rover->master->sidedef[0] : NULL;
				FTextureID texid;
				int texpos;
				if (rover->flags & FF_UPPERTEXTURE)      { texid = seg->sidedef->GetTexture(side_t::top);    texpos = side_t::top; }
				else if (rover->flags & FF_LOWERTEXTURE) { texid = seg->sidedef->GetTexture(side_t::bottom); texpos = side_t::bottom; }
				else if (mastersd != NULL)               { texid = mastersd->GetTexture(side_t::mid);        texpos = side_t::mid; }
				else continue;

				const void *fmat = zx::materials::MaterialForTextureId(texid);
				if (fmat == NULL) { MeshSquash(range); continue; }

				FTexCoordInfo tci;
				const side_t *scalesd = (rover->flags & (FF_UPPERTEXTURE | FF_LOWERTEXTURE)) ?
					seg->sidedef : mastersd;
				zx::materials::MaterialTexCoordInfo(fmat, &tci, scalesd->GetTextureXScale((side_t::ETexpart)texpos),
					scalesd->GetTextureYScale((side_t::ETexpart)texpos));
				if (tci.mRenderHeight == 0) { MeshSquash(range); continue; }

				// u: the master's x offset plus this sidedef's, over the line's texel length -- the
				// same pair BuildFFBlock adds together.
				const float toU = (rover->flags & (FF_UPPERTEXTURE | FF_LOWERTEXTURE)) ? 0.f :
					FIXED2FLOAT(tci.TextureOffset(mastersd->GetTextureXOffset(side_t::mid)));
				const float ul = tci.FloatToTexU(toU +
					FIXED2FLOAT(tci.TextureOffset(seg->sidedef->GetTextureXOffset(side_t::mid))));
				const float uR = ul + tci.FloatToTexU(seg->sidedef->TexelLength);

				// v: measured DOWN from the slab's own texture height, which is the plane's, not the
				// wall's -- a 3D floor's texture hangs off the slab and not off the sector.
				float toV = (rover->flags & (FF_UPPERTEXTURE | FF_LOWERTEXTURE)) ? 0.f :
					FIXED2FLOAT(tci.RowOffset(mastersd->GetTextureYOffset(side_t::mid)));
				toV += FIXED2FLOAT(tci.RowOffset(seg->sidedef->GetTextureYOffset(side_t::mid)));
				const float texh = FIXED2FLOAT(*rover->top.texheight);
				const float vt0 = tci.FloatToTexV(toV + texh - blk.top[0]);
				const float vt1 = tci.FloatToTexV(toV + texh - blk.top[1]);
				const float vb0 = tci.FloatToTexV(toV + texh - blk.bottom[0]);
				const float vb1 = tci.FloatToTexV(toV + texh - blk.bottom[1]);

				MeshVertex fan[4];
				fan[0].Set(mid.x1, blk.bottom[0], mid.y1, ul, vb0);
				fan[1].Set(mid.x1, blk.top[0],    mid.y1, ul, vt0);
				fan[2].Set(mid.x2, blk.top[1],    mid.y2, uR, vt1);
				fan[3].Set(mid.x2, blk.bottom[1], mid.y2, uR, vb1);
				int fw = 0;
				for (int t = 0; t < 2; t++)
					for (int cc = 0; cc < 3; cc++)
						tris[fw++] = fan[ComputeFanTriangleVertex(4, t, cc)];

				if (!MeshStore(range, tris, fw)) { MeshSquash(range); continue; }

				MeshPiece mp;
				mp.range = range;
				mp.material = fmat;
				// A slab's own planes are usually the light boundaries, so the block lands inside
				// one band -- and where it straddles, its first piece takes the band SplitWall
				// would have given it.
				int ffLight = dl.lightLevel;
				FColormap cm;
				cm = cmFrom;
				{
					zx::surfaces::WallBand bb[kBandsPerPart];
					const int nbb = zx::surfaces::ComputeWallBands(blk.top, blk.bottom,
						ffBandBottom, nLights, bb, kBandsPerPart);
					sc.bandLight[kFFSlot0 + bi] =
						(nbb > 0 && !bb[0].ownLight && bb[0].lightIndex < nLights)
							? (signed char)bb[0].lightIndex : -1;
					if (nbb > 0 && !bb[0].ownLight && bb[0].lightIndex < nLights)
					{
						const lightlist_t &ll = (*lights)[bb[0].lightIndex];
						if (ll.p_lightlevel != &seg->sidedef->sector->lightlevel)
							ffLight = gl_ClampLight(*ll.p_lightlevel);
						cm.CopyLightColor(ll.extra_colormap);
					}
				}
				zx::levelmesh::CaptureShading(ffLight, dl.relLight + getExtraLight(), cm, mp, false);
				mp.baseTex = TexMan.ByIndex(texid.GetIndex());
				mp.lightLevel = ffLight;
				mp.lightColor = cm.LightColor.d;
				mp.fadeColor = cm.FadeColor.d;
				// A translucent or additive slab is not opaque, the way a 3D floor's own plane is not.
				if (rover->flags & (FF_TRANSLUCENT | FF_ADDITIVETRANS))
				{
					mp.alpha = rover->alpha / 255.0f;
					mp.blendMode = ComputeSurfaceBlendMode((rover->flags & FF_ADDITIVETRANS) != 0,
						mp.alpha);
				}
				{
					const float dx = mid.x2 - mid.x1, dy = mid.y2 - mid.y1;
					const float len = sqrtf(dx*dx + dy*dy);
					if (len > 0.0001f) { mp.normX = dy / len; mp.normY = 0.f; mp.normZ = -dx / len; }
				}
				RegisterSegPiece(mp, seg);
				made++;
			}
			for (int k = nb; k < kMaxFFBlocks; k++) MeshSquash(sc.pieces[kFFSlot0 + k].range);
		}
		else
		{
			for (int k = 0; k < kMaxFFBlocks; k++) MeshSquash(sc.pieces[kFFSlot0 + k].range);
		}
	}
	else
	{
		for (int k = 0; k < kMaxFFBlocks; k++) MeshSquash(sc.pieces[kFFSlot0 + k].range);
	}

	sc.bakedCount = kMaxMapPieces;
	return made;
}

// [rc4l] Every wall in the level, from the map, in one pass; how many parts it built.
int BakeLevelFromMap()
{
	if (segs == NULL || numsegs <= 0) return 0;
	int made = 0;
	for (int i = 0; i < numsegs; i++) { const int m = BakeSegFromMap(i); if (m > 0) made += m; }
	return made;
}

// [rc4l] How many vertices the mesh actually holds for one part of one seg.
//
// Every other question about the map bake has been asked of the derivation, which can answer "yes I
// would build that" for a part the bake then does not build -- because the bake also needs a light,
// and needs MeshStore to accept it. This asks the MESH, which is the only thing the frame is drawn
// from. Part is 0 upper, 1 lower, 2 middle, matching BakeSegFromMap's slots.
int MapBakePartCount(int segIndex, int part)
{
	if ((unsigned)segIndex >= g_cache.Size()) return -1;
	if (part < 0 || part >= 3) return -1;
	// [rc4l] A part is spread across its BANDS, so the answer is their sum and not one slot.
	//
	// Reading slot `part` alone was right until a wall could be cut into bands and the lower moved
	// from slot 1 to slot 5. The probe then reported 718 parts missing from a mesh that had all of
	// them, which is a diagnostic lying in the direction that costs the most time.
	int total = 0;
	for (int k = 0; k < kBandsPerPart; k++)
		total += (int)g_cache[segIndex].pieces[part * kBandsPerPart + k].range.count;
	return total;
}


void InvalidateAll()
{
	// [rc4l] Flats share the same mesh, so they invalidate together or a backend sees a half-stale world.
	zx::levelmesh::ClearFlats();
	zx::levelmesh::ClearSprites();
}


void GetCoverage(CoverageStats &out)
{
	out.segs = (int)g_cache.Size();
	out.baked = out.pieces = 0;
	// [rc4l] Minisegs -- BSP-generated segs with no sidedef -- draw nothing at all, so counting them
	// against coverage understates it badly. Sunder MAP10 has 54845 segs but only 26691 sides, and
	// without this split "28% of segs baked" reads as a bug rather than as arithmetic.
	out.drawableSegs = 0;
	for (int i = 0; i < numsegs; i++)
		if (segs[i].sidedef != NULL) out.drawableSegs++;
	for (unsigned i = 0; i < g_cache.Size(); i++)
	{
		int p = 0;
		for (int k = 0; k < g_cache[i].pieceCount; k++)
			if (g_cache[i].pieces[k].range.count > 0) p++;
		if (p > 0) out.baked++;
		out.pieces += p;
	}
}

// [rc4l] How many sectors moved on the last frame InvalidateMovedSectors ran, for its stats.
static int g_movedThisFrame = 0;

// [rc4l] Why a sector was dirty, not just that it was.
//
// InvalidateMovedSectors treats every dirty sector as one that MOVED and answers with a full
// geometry re-derive. But sector_t bumps fua_dirty for light changes too, and Doom's light effects
// -- flicker, strobe, glow -- run every tic. So a room with a flickering lamp can be re-deriving its
// walls from the map several times a second to express a light level. Counted here so the guess
// stops being a guess.
int g_dirtyLightOnly = 0, g_dirtyGeometry = 0;
// [rc4l] One timer for the WHOLE loop, not one per piece.
//
// An earlier profile put a timer around each piece and spent twelve thousand clock reads a frame
// measuring a frame it had made slower. The question here is only "how much of the frame is this
// loop", so one read either side answers it and costs nothing.
static double g_invalMs = 0.0; static int g_invalFrames = 0; static int g_segsBaked = 0;
static int g_segsReshaded = 0;
// The last frame's own figures, not the running totals -- so a frame-time breakdown can ask what
// THIS frame spent rather than what the level has spent since it loaded.
static double g_invalMsLast = 0.0; static int g_sectorsLast = 0;   // segs that took the light-only path instead of a full re-derive
static TArray<SectorLook> g_lastLook;
static int g_dirtyNoChange = 0;   // dirty, but nothing that shows on a wall actually differed
// [rc4l] Per sector the tic a relight was first left for a later frame, -1 for none.
static TArray<int> g_relightHeld;
static int g_relightsHeld = 0;

double InvalidationMsLastFrame() { return g_invalMsLast; }
int    SectorsDirtyLastFrame()   { return g_sectorsLast; }

void SectorDirtyBreakdown(int &lightOnly, int &geometry)
{
	lightOnly = g_dirtyLightOnly;
	geometry  = g_dirtyGeometry;
}

// [rc4l] Re-run the whole-map wall bake now, to tell a bake that was wrong from one that went stale.
CCMD( fua_mapbake_now )
{
	Printf( "%d wall parts rebuilt from the map\n", zx::levelmesh::BakeLevelFromMap( ) );
}

CCMD( fua_bake_dirty )
{
	Printf( "sector invalidations since load: %d light-only, %d moved a plane, %d re-baked in the last frame\n",
		g_dirtyLightOnly, g_dirtyGeometry, g_movedThisFrame );
	Printf( "  %d were dirty with nothing visibly changed\n", g_dirtyNoChange );
	Printf( "  %d relights left for a later frame by fua_relight_budget_us\n", g_relightsHeld );
	Printf( "  scrolled in place: %d sides, %d flats\n", g_sidesRescrolled, zx::levelmesh::GetFlatRescrolls( ) );
	Printf( "  reshade declined: %d 3D-floor, %d no light, %d not the side owner, %d piece missing\n",
		g_reshadeFF, g_reshadeNoLight, g_reshadeNotOwner, g_reshadeNoPiece );
	{
		unsigned int used = 0, parked = 0; int reuses = 0, reallocs = 0;
		zx::levelmesh::MeshArenaStats( used, parked, reuses, reallocs );
		Printf( "  arena: %u verts held, %u parked on the free list, %d reused, %d size changes\n",
			used, parked, reuses, reallocs );
		unsigned int pieces = 0, indexed = 0;
		zx::levelmesh::MeshPieceListStats( pieces, indexed );
		Printf( "  pieces: %u records, %u offsets indexed\n", pieces, indexed );
	}
	if ( g_invalFrames > 0 )
		Printf( "  invalidation loop: %.3f ms/frame over %d frames, %.1f baked + %.1f reshaded per frame\n",
			g_invalMs / g_invalFrames, g_invalFrames,
			(double)g_segsBaked / g_invalFrames, (double)g_segsReshaded / g_invalFrames );
	if ( argv.argc() > 1 )
	{
		g_dirtyLightOnly = 0; g_dirtyGeometry = 0; g_dirtyNoChange = 0; g_relightsHeld = 0;
		g_reshadeFF = 0; g_reshadeNoLight = 0; g_reshadeNotOwner = 0; g_reshadeNoPiece = 0;
		g_invalMs = 0.0; g_invalFrames = 0; g_segsBaked = 0; g_segsReshaded = 0;
		g_sidesRescrolled = 0;
		Printf( "  (counters reset)\n" );
	}
}

// [rc4l] Re-bake one seg whose sector or sidedef changed: re-light it when only the light moved, re-derive it otherwise.
static void RebakeChangedSeg(int idx, bool geometryChanged)
{
	if ((unsigned)idx >= g_cache.Size()) return;
	SegCache &sc = g_cache[idx];
	// [rc4l] Bake first and squash only what the bake declined, since squashing zeroes the range and defeats MeshStore's compare-before-upload (3.7x slower on wdiex02's flickering lights).
	int made = geometryChanged ? -2 : ReshadeSegFromMap(idx);
	if (made == -1 || made == -2)
	{
		made = BakeSegFromMap(idx);
		g_segsBaked++;
	}
	else g_segsReshaded++;
	if (made < 0)   // [rc4l] declined: nothing of this seg is drawn now
	{
		// [rc4l] Squash, not release, so a later bake writes back over a range that stays allocated instead of the arena growing every frame the sector moves.
		for (int i = 0; i < kMaxMapPieces; i++)
			MeshSquash(sc.pieces[i].range);
	}
}

void RebakeTransferSegs()
{
	if (!gl_wallmesh) return;
	for (int s = 0; s < numsectors && (unsigned)s < g_sectorSegs.Size(); s++)
	{
		if (!ViewDependent(&sectors[s])) continue;
		for (unsigned k = 0; k < g_sectorSegs[s].Size(); k++) RebakeChangedSeg(g_sectorSegs[s][k], true);
	}
}

// [rc4l] A sector, then every sector it is a 3D floor in (XFloor.attached), whose planes, sides and light bands are drawn from it.
template <typename Fn>
static void ForSectorAndTargets(int s, Fn fn)
{
	fn(s);
	if (sectors[s].e == NULL) return;
	const TArray<sector_t *> &targets = sectors[s].e->XFloor.attached;
	for (unsigned k = 0; k < targets.Size(); k++) fn(int(targets[k] - sectors));
}

// [rc4l] A sector's own planes, which have no walk to notice them change, and the fake planes other subsectors copy from it.
static void RebakeSectorFlats(int sector)
{
	zx::levelmesh::BakeFlatsForSector(sector);
	zx::levelmesh::RebakeFakesModelledOn(sector);
}

static SectorLook LookOf(const sector_t &s)
{
	SectorLook l;
	l.light = s.lightlevel;
	l.floorD = s.floorplane.d.Raw();
	l.ceilD = s.ceilingplane.d.Raw();
	l.floorPic = s.GetTexture(sector_t::floor).GetIndex();
	l.ceilPic = s.GetTexture(sector_t::ceiling).GetIndex();
	l.colormap = s.ColorMap;
	return l;
}

static long long RelightClockUs()
{
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// [rc4l] A sector's change since the last look, re-baking its walls and flats and those of any sector it is a 3D floor in; a relight past the frame's budget waits, still dirty, for a later frame.
static void InvalidateSector(int s, TArray<int> &fillMoved, long long budgetStartUs)
{
	if (s < 0 || s >= numsectors || (unsigned)s >= g_sectorDirty.Size()) return;
	if (g_sectorDirty[s] == sectors[s].fua_dirty) return;
	// [rc4l] fua_dirty says only that a setter ran (SetLightLevel, ChangeLightLevel, SetTexture, SetPlaneTexZ, ChangePlaneTexZ, SetColor, SetFade), and a strobe re-asserts its held level every tic, so a sector whose look did not change is skipped.
	const SectorLook now = LookOf(sectors[s]);
	const int change = ComputeSectorRebake(g_lastLook[s], now);
	// [rc4l] Every light effect in a level changes its sector on the tic, so the frame after it re-lit them all at once.
	// [rc4l] The clock is read only for a relight: once for each of a few thousand walked sectors a frame cost more than the budget saved.
	const bool overBudget = change == kSectorRelit && fua_relight_budget_us > 0 && RelightClockUs() - budgetStartUs > fua_relight_budget_us;
	if (ComputeHoldRelight(change, overBudget, g_relightHeld[s], gametic))
	{
		if (g_relightHeld[s] < 0) g_relightHeld[s] = gametic;
		g_relightsHeld++;
		return;
	}
	g_relightHeld[s] = -1;
	g_sectorDirty[s] = sectors[s].fua_dirty;
	g_movedThisFrame++;
	g_lastLook[s] = now;
	if (change == kSectorSame) { g_dirtyNoChange++; return; }
	if (change == kSectorMoved) g_dirtyGeometry++; else g_dirtyLightOnly++;
	// A texture change alters UVs, so only a pure relight may take the reshade path.
	const bool sectorMoved = change >= kSectorRetextured;
	if (sectorMoved) fillMoved.Push(s);

	// [rc4l] A 3D floor's control sector moves or relights the floors, sides and light bands of the sectors it sits in, so those go again with it.
	ForSectorAndTargets(s, [sectorMoved](int t) {
		const TArray<int> &segList = g_sectorSegs[t];
		// [rc4l] Planes that held still cannot have moved the walls, so those segs only re-light (ReshadeSegFromMap) and anything else takes the full bake.
		for (unsigned k = 0; k < segList.Size(); k++) RebakeChangedSeg(segList[k], sectorMoved);
		RebakeSectorFlats(t);
	});
}

// [rc4l] A sidedef a setter touched: a switch's texture, a scroller's offset, which no sector change reports.
static void InvalidateSide(int i, TArray<int> &fillSides)
{
	if (i < 0 || i >= numsides || (unsigned)i >= g_sideDirty.Size()) return;
	if (!ComputeTakeChange(g_sideDirty[i], sides[i].fua_dirty)) return;
	if (zx::levelmesh::SideTexturesChanged(i)) fillSides.Push(i);
	if (RescrollSide(i)) { g_sidesRescrolled++; return; }
	for (int k = 0; k < sides[i].numsegs; k++) RebakeChangedSeg(int(sides[i].segs[k] - segs), true);
}

// [rc4l] A sector whose floor or ceiling texture a scroller, its interpolation or a script placed: its flats and those of any sector it is a 3D floor in, since no wall is drawn from it.
static void ReplaceSectorFlats(int s)
{
	if (s < 0 || s >= numsectors || (unsigned)s >= g_sectorPlacement.Size()) return;
	if (ComputeTakeChange(g_sectorPlacement[s], sectors[s].fua_placement)) ForSectorAndTargets(s, RebakeSectorFlats);
}

// [rc4l] Re-bakes from the map every seg and flat of a sector whose look changed, visible or not, so a moved far face never keeps its old quad.
void InvalidateMovedSectors(const WalkedThisFrame *walked)
{
	g_movedThisFrame = 0;
	cycle_t invalClock; invalClock.Reset(); invalClock.Clock();
	g_invalFrames++;
	if ((int)g_lastLook.Size() != numsectors)
	{
		g_lastLook.Resize((unsigned)numsectors);
		for (int i = 0; i < numsectors; i++) g_lastLook[i] = LookOf(sectors[i]);
		g_relightHeld.Resize((unsigned)numsectors);
		for (int i = 0; i < numsectors; i++) g_relightHeld[i] = -1;
	}
	if (!gl_wallmesh || g_sectorDirty.Size() == 0) return;
	// [rc4l] GL's missing-texture fills hang on the heights and textures round them, so a move or a texture change works them out again below.
	TArray<int> fillMoved, fillSides;
	// [rc4l] With the walks' lists only what they reached is looked at, and the rest stays dirty until a walk reaches it.
	const int sectorCount = walked != NULL ? walked->sectorCount : numsectors;
	const int sideCount = walked != NULL ? walked->sideCount : numsides;
	const long long budgetStart = RelightClockUs();
	for (int k = 0; k < sectorCount; k++) InvalidateSector(walked != NULL ? walked->sectors[k] : k, fillMoved, budgetStart);
	for (int k = 0; k < sideCount; k++) InvalidateSide(walked != NULL ? walked->sides[k] : k, fillSides);
	for (int k = 0; k < sectorCount; k++) ReplaceSectorFlats(walked != NULL ? walked->sectors[k] : k);
	if (fillMoved.Size() > 0 || fillSides.Size() > 0) zx::levelmesh::RefreshFakePlanes(fillMoved, fillSides);
	invalClock.Unclock();
	g_invalMs += invalClock.TimeMS();
	g_invalMsLast = invalClock.TimeMS();
	g_sectorsLast = g_movedThisFrame;
}

// [rc4l] Read-only views of one seg's cache state, for fua_line_mesh. The cache array is file-static
// on purpose; exposing two getters beats exporting the storage.
void GetSegMeshInfo(int segIndex, int &pieces, int &baked)
{
	pieces = baked = 0;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	pieces = g_cache[segIndex].pieceCount;
	baked  = g_cache[segIndex].bakedCount;
}

// [rc4l] The light-list entry a baked slot took its light from, or -1 for the wall's own light.
int GetSegBandLight(int segIndex, int piece)
{
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size() || piece < 0 || piece >= kMaxMapPieces) return -1;
	return g_cache[segIndex].bandLight[piece];
}

void GetSegPieceRange(int segIndex, int piece, unsigned int &offset, unsigned int &count)
{
	offset = count = 0;
	if (segIndex < 0 || (unsigned)segIndex >= g_cache.Size()) return;
	if (piece < 0 || piece >= kMaxMapPieces) return;
	offset = g_cache[segIndex].pieces[piece].range.offset;
	count  = g_cache[segIndex].pieces[piece].range.count;
}

}} // namespace zx::levelmesh

// [rc4l] The flat cache, which had no cache at all until it was measured: every visible flat was rebuilt from scratch every frame, 3406 times a frame on Sunder MAP16.
CCMD( fua_flatcache_stats )
{
	int fh = 0, fr = 0;
	zx::levelmesh::GetFlatCacheStats( fh, fr );
	Printf( "flats: %d left alone, %d rebuilt, %d reshaded (%.1f%% hit)\n", fh, fr, zx::levelmesh::GetFlatReshades( ),
		fh + fr > 0 ? 100.0 * (double)fh / (double)( fh + fr ) : 0.0 );
}

//==========================================================================
//
// fua_line_mesh <linedef>
//
// [rc4l] What the MESH holds for one linedef, as opposed to what the screen shows.
//
// A door that stayed shut in the Vulkan view while GL showed it open produced three plausible
// theories -- stale piece map, uncollapsed slot, cache hit that should have missed -- and screenshots
// could not tell them apart, because every one of them looks like "the old door". The heights of the
// baked vertices settle it in one line: if the mesh still spans the closed door, the bake never ran;
// if it does not, the bake ran and the backend is drawing something else.
//
//==========================================================================

CCMD( fua_line_mesh )
{
	if ( lines == NULL || argv.argc( ) < 2 )
	{
		Printf( "usage: fua_line_mesh <linedef index>\n" );
		return;
	}
	const int want = atoi( argv[1] );
	if ( want < 0 || want >= numlines ) { Printf( "no such line\n" ); return; }

	int nv = 0;
	const zx::levelmesh::MeshVertex *verts = zx::levelmesh::MeshVertexData( nv );
	int found = 0;
	for ( int i = 0; i < numsegs; i++ )
	{
		if ( segs[i].linedef == NULL || int( segs[i].linedef - lines ) != want ) continue;
		found++;
		int pieces = 0, baked = 0;
		zx::levelmesh::GetSegMeshInfo( i, pieces, baked );
		Printf( "seg %d: %d pieces (%d baked last time)\n", i, pieces, baked );
		// [rc4l] A map-baked seg keeps its geometry in the baked slots, not the captured pieces, so list whichever it has.
		const int listed = ( pieces > baked ) ? pieces : baked;
		for ( int k = 0; k < listed; k++ )
		{
			unsigned off = 0, cnt = 0;
			zx::levelmesh::GetSegPieceRange( i, k, off, cnt );
			if ( cnt == 0 ) continue;
			float zlo = 1e9f, zhi = -1e9f;
			for ( unsigned v = 0; v < cnt && off + v < (unsigned)nv; v++ )
			{
				const float z = verts[off + v].z;
				if ( z < zlo ) zlo = z;
				if ( z > zhi ) zhi = z;
			}
			Printf( "   piece %d: range %u+%u, z %.1f..%.1f, band light %d\n", k, off, cnt, zlo, zhi,
				zx::levelmesh::GetSegBandLight( i, k ) );
		}
	}
	if ( found == 0 ) Printf( "line %d has no segs\n", want );
}
