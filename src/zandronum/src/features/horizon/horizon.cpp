// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/horizon/horizon.h"
#include "features/horizon/computation/horizon_compute.h"
#include "features/levelmesh/flatmesh.h"     // MakeWallQuad
#include "features/levelmesh/wallcache.h"    // LevelGeneration
#include "features/levelmesh/captureshading.h"   // [rc4l] CaptureShading, the light model for a GL colormap

#include "r_defs.h"
#include "r_state.h"
#include "r_sky.h"                        // skyflatnum
#include "p_3dfloors.h"                   // P_GetPlaneLight
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "features/materials/materials.h"
#include "features/surfaces/sectorplane.h"   // [rc4l] GLSectorPlane
#include "c_cvars.h"
#include "tarray.h"

// [rc4l] Draw horizon lines as windows onto their planes; off leaves them empty, for an A/B.
CVAR(Bool, fua_dg_horizon, true, 0)

EXTERN_CVAR(Bool, gl_wallmesh)

namespace zx { namespace horizon {

struct HorizonSide
{
	int line;
	int side;
};

static TArray<HorizonSide>   g_sides;
static int                   g_sidesFor = -1;
static int                   g_sidesLines = -1;
static TArray<HorizonWindow> g_windows;
static TArray<zx::levelmesh::MeshVertex>   g_skyVerts;

bool HorizonsActive() { return fua_dg_horizon; }

const HorizonWindow *HorizonWindows(int &count)
{
	count = (int)g_windows.Size();
	return count ? &g_windows[0] : NULL;
}

const zx::levelmesh::MeshVertex *HorizonSkyVertices(int &count, float eyeZ)
{
	g_skyVerts.Clear();
	for (unsigned i = 0; i < g_windows.Size(); i++)
	{
		const HorizonWindow &w = g_windows[i];
		if (!w.skyAbove) continue;
		const float bottom = ComputeHorizonSkyBottom(w.floor.valid, w.bottom, w.floorZ, w.ceilingZ, eyeZ);
		if (bottom >= w.top) continue;
		const float zb[2] = { bottom, bottom }, zt[2] = { w.top, w.top };
		zx::levelmesh::MeshVertex q[6];
		zx::levelmesh::MakeWallQuad(q, w.x1, w.y1, w.x2, w.y2, zb, zt);
		for (int k = 0; k < 6; k++) g_skyVerts.Push(q[k]);
	}
	count = (int)g_skyVerts.Size();
	return count ? &g_skyVerts[0] : NULL;
}

// [rc4l] Which sidedefs are horizons, found once per level since the special does not change in play.
static void RescanIfNeeded()
{
	const int gen = zx::levelmesh::LevelGeneration();
	if (gen == g_sidesFor && numlines == g_sidesLines) return;
	g_sidesFor = gen;
	g_sidesLines = numlines;
	g_sides.Clear();
	for (int i = 0; i < numlines; i++)
	{
		if (!ComputeIsHorizonLine(lines[i].special)) continue;
		for (int s = 0; s < 2; s++)
		{
			if (lines[i].sidedef[s] == NULL || lines[i].sidedef[s]->sector == NULL) continue;
			HorizonSide hs;
			hs.line = i;
			hs.side = s;
			g_sides.Push(hs);
		}
	}
}

// [rc4l] One plane, by DoHorizon's light rule and GLHorizonPortal::DrawContents's shading.
static void BuildPlane(sector_t *sec, int which, HorizonPlane &out)
{
	GLSectorPlane sp;
	sp.GetFromSector(sec, which);
	out.valid = true;
	out.z = FIXED2FLOAT(sp.texheight);
	out.shade = zx::levelmesh::MeshPiece();
	out.uv = zx::levelmesh::ComputePlaneUVAffine(zx::levelmesh::ComputeIdentityPlaneUV());

	const void *mat = zx::materials::MaterialForTextureId(sp.texture);
	out.material = mat;
	if (mat == NULL)
	{
		// [rc4l] GL clears a horizon with no texture to black, which is what a zero light paints.
		out.shade.colorR = out.shade.colorG = out.shade.colorB = 0.f;
		return;
	}

	zx::levelmesh::PlaneUVTransform t;
	zx::levelmesh::PlaneUVFor(sp, mat, t);
	out.uv = zx::levelmesh::ComputePlaneUVAffine(t);

	const bool ceiling = (which == sector_t::ceiling);
	int lightlevel = gl_ClampLight(ceiling ? sec->GetCeilingLight() : sec->GetFloorLight());
	FColormap cm;
	cm = sec->ColorMap;
	if (sec->e != NULL && sec->e->XFloor.ffloors.Size())
	{
		lightlist_t *light = P_GetPlaneLight(sec, ceiling ? &sec->ceilingplane : &sec->floorplane, ceiling);
		if (!(sec->GetFlags(which) & PLANEF_ABSLIGHTING)) lightlevel = gl_ClampLight(*light->p_lightlevel);
		cm.LightColor = (light->extra_colormap)->Color;
	}
	int level, rel;
	ComputeHorizonLightInputs(zx::materials::MaterialIsFullbright(mat), lightlevel, getExtraLight(), level, rel);
	zx::levelmesh::CaptureShading(level, rel, cm, out.shade);
}

void UpdateHorizons()
{
	g_windows.Clear();
	if (!fua_dg_horizon || !gl_wallmesh || lines == NULL || numlines <= 0) return;
	RescanIfNeeded();

	for (unsigned i = 0; i < g_sides.Size(); i++)
	{
		const line_t &ln = lines[g_sides[i].line];
		const bool back = (g_sides[i].side == 1);
		sector_t *sec = ln.sidedef[g_sides[i].side]->sector;

		HorizonWindow w;
		// [rc4l] The back side runs v2 to v1, which keeps its own sector on the right.
		const vertex_t *a = back ? ln.v2 : ln.v1;
		const vertex_t *b = back ? ln.v1 : ln.v2;
		w.x1 = FIXED2FLOAT(a->x); w.y1 = FIXED2FLOAT(a->y);
		w.x2 = FIXED2FLOAT(b->x); w.y2 = FIXED2FLOAT(b->y);
		w.floorZ = FIXED2FLOAT(sec->GetPlaneTexZ(sector_t::floor));
		w.ceilingZ = FIXED2FLOAT(sec->GetPlaneTexZ(sector_t::ceiling));
		const bool floorSky = sec->GetTexture(sector_t::floor) == skyflatnum;
		const bool ceilingSky = sec->GetTexture(sector_t::ceiling) == skyflatnum;
		const HorizonHalves halves = ComputeHorizonHalves(floorSky, ceilingSky);
		const HorizonSpan span = ComputeHorizonWindowSpan(w.floorZ, w.ceilingZ, floorSky, ceilingSky);
		w.bottom = span.bottom;
		w.top = span.top;
		w.floor.valid = w.ceiling.valid = false;
		if (halves.floorPlane) BuildPlane(sec, sector_t::floor, w.floor);
		if (halves.ceilingPlane) BuildPlane(sec, sector_t::ceiling, w.ceiling);
		w.skyAbove = halves.skyAbove;
		g_windows.Push(w);
	}
}

}} // namespace zx::horizon
