// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Dynamic lights: the light list mirrored from the engine, GL's light links, and the cluster grid that tells a fragment which lights can reach it.


#include <vector>

#include "MapHelper.hpp"
#include "RefCntAutoPtr.hpp"

#include "features/hwrender/diligent/dgshared.h"
#include "features/hwrender/computation/lightcluster_compute.h"
#include "features/hwrender/computation/lighttouch_compute.h"
#include "features/levelmesh/levelmesh.h"
#include "features/levelmesh/wallcache.h"
#include "features/dynlights/dynlight.h"
#include "features/gamecopy/lights.h"   // [rc4l] the light records
#include "features/gamecopy/actors.h"   // [rc4l] CopyTic
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "p_local.h"
#include "doomstat.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "tarray.h"

EXTERN_CVAR(Float, r_lights_size)
EXTERN_CVAR(Float, r_lights_intensity)
EXTERN_CVAR(Bool, r_lights_additive)
EXTERN_CVAR(Bool, fua_dg_dynlights)
EXTERN_CVAR(Bool, fua_dg_lightlinks)
EXTERN_CVAR(Bool, fua_dg_clusters)

namespace zx { namespace hwrender {

// [rc4l] The dynamic light buffer, mirrored from the engine every frame.
//
// A structured (storage) buffer rather than a uniform block, matching the engine's own preferred
// path: the light list is variable-length and indexed per surface, which is exactly what a UBO is
// bad at. Grow-only, and uploaded once per engine frame -- the same generation guard the sprite
// stream uses, for the same reason (Vulkan's dynamic heap is per frame, not per draw).
// [rc4l] The cluster grid: which lights can reach each cell of the view.
//
// g_clusterBuf holds one (offset, count) pair per cell and g_lightIndexBuf the runs those point
// into, so a fragment reads its own cell and tests a handful of lights instead of all of them. The
// binning is on the CPU for now -- correct and measurable first, a compute shader after, in that
// order, because a wrong answer computed on the GPU is far harder to look at.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_clusterBuf;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lightIndexBuf;
static unsigned int g_clusterCapacity = 0;     // cells
static unsigned int g_lightIndexCapacity = 0;  // entries
static int g_clusterCells = 0;                 // cells actually in use this frame
static int g_clusterRefs = 0;                  // light-in-cell entries this frame, for the stats
static int g_clusterTruncated = 0;             // cells that did not fit, this frame
static bool g_clusterBindFailed = false;

// The lights of this frame as the shader will see them: eight floats each, position (x, z, y),
// radius, colour, mode. The binning pass reads THESE rather than walking the thinkers again, so a
// light the collector rejected cannot reappear in a cell.
static TArray<float> g_lightData;

static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lightBuf;
static unsigned int g_lightBufCapacity = 0;   // in vec4s
// [rc4l] Each light's run in the sorted list of subsector and sidedef ids GL linked it to, made once at fixed sizes since the pipelines bind them statically.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lightTouchRangeBuf, g_lightTouchIdBuf;
static const unsigned int kLightTouchIdCapacity = 262144;
static int g_lightCount = 0;
static bool g_lightBindFailed = false;

bool EnsureLightBuffers(FString &err)
{
	// [rc4l] The dynamic light buffer, created ONCE at a fixed capacity.
	//
	// It is bound to the pipelines as a STATIC variable, which Diligent copies into every SRB at
	// creation -- so recreating the buffer to grow it would silently leave existing SRBs pointing at
	// the freed one. 65536 vec4s is 1 MB and holds ~32000 lights; a Doom frame produces a handful.
	if (!g_lightBuf)
	{
		g_lightBufCapacity = 65536;
		if (!CreateStructuredBuffer("fua light buffer", 16, g_lightBufCapacity, NULL, g_lightBuf))
		{ err = "light buffer creation failed"; return false; }
		// [rc4l] The light links, one range per light the buffer holds and a fixed id list, created once like everything else the world pipelines bind.
		if (!CreateStructuredBuffer("fua light link ranges", 2 * sizeof(unsigned), g_lightBufCapacity / 2, NULL, g_lightTouchRangeBuf) ||
			!CreateStructuredBuffer("fua light link ids", sizeof(unsigned), kLightTouchIdCapacity, NULL, g_lightTouchIdBuf))
		{ err = "light link buffer creation failed"; return false; }
	}
	// [rc4l] The cluster table and its index list, created once (a buffer recreated mid-frame leaves every SRB pointing at the freed one) for the grid every frame has.
	if (!g_clusterBuf)
	{
		g_clusterCapacity = zx::hwrender::kClusterCells;
		if (!CreateStructuredBuffer("fua cluster table", 8, g_clusterCapacity, NULL, g_clusterBuf))
		{ err = "cluster table creation failed"; return false; }
	}
	if (!g_lightIndexBuf)
	{
		// [rc4l] A million entries, 4 MB, because a quarter of that was not enough and the shortfall
		// was invisible. 4096 lights spread over a map bin to ~385k entries; 262144 truncated that
		// and the picture went 85% wrong with nothing but a number in a stats line to say so.
		g_lightIndexCapacity = 1048576;
		if (!CreateStructuredBuffer("fua cluster light indices", 4, g_lightIndexCapacity, NULL, g_lightIndexBuf))
		{ err = "cluster index buffer creation failed"; return false; }
	}
	return true;
}

void BindLightBuffers(Diligent::IPipelineState *pso)
{
	// [rc4l] Reported rather than skipped when a variable is missing, since one that binds nothing reads as "dynamic lights do nothing" with no error anywhere.
	auto *lv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightBuffer");
	if (lv) lv->Set(g_lightBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	else g_lightBindFailed = true;
	if (auto *tv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightTouchRanges"))
		tv->Set(g_lightTouchRangeBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	if (auto *tv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightTouchIds"))
		tv->Set(g_lightTouchIdBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	auto *cv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "ClusterTable");
	if (cv) cv->Set(g_clusterBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	else g_clusterBindFailed = true;
	auto *iv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "ClusterLightIndices");
	if (iv) iv->Set(g_lightIndexBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
	else g_clusterBindFailed = true;
}

int LightCount() { return g_lightCount; }
int ClusterCells() { return g_clusterCells; }

void AppendLightStats(FString &report)
{
	FString clusters;
	clusters.Format(" | clusters: %d cells, %d light refs (%.1f per occupied cell)",
		g_clusterCells, g_clusterRefs,
		g_clusterCells > 0 ? (float)g_clusterRefs / (float)g_clusterCells : 0.f);
	report += clusters;
	if (g_clusterTruncated > 0)
	{
		FString trunc;
		trunc.Format(" | WARNING: cluster index buffer full, %d cells truncated", g_clusterTruncated);
		report += trunc;
	}
	if (g_clusterBindFailed) report += " | WARNING: ClusterTable not bound";
	if (g_lightBindFailed) report += " | WARNING: LightBuffer not bound";
}

//==========================================================================
//
// fua_dg_lights
//
// [rc4l] Every dynamic light the backend can see, with the numbers the shader actually tests.
//
// The shader decides whether a light reaches a surface by comparing the light against the
// surface plane, so a light sitting exactly ON a plane is the one case where that comparison has
// no right answer -- and a floor lit by such a light is at the mercy of the last bit of an
// interpolated coordinate. Height is printed against the player floor for that reason: it is the
// difference, not the absolute position, that says whether a light is in that state.
//
//==========================================================================

CCMD( fua_dg_lights )
{
	const float floorz = ( players[consoleplayer].mo != NULL )
		? FIXED2FLOAT( players[consoleplayer].mo->floorz ) : 0.f;
	int n = 0;
	TThinkerIterator<ADynamicLight> it( STAT_DLIGHT );
	ADynamicLight *light;
	while ( ( light = it.Next( ) ) != NULL )
	{
		if ( !light->IsActive( ) ) continue;
		const float radius = light->GetRadius( ) * r_lights_size;
		if ( radius <= 0.f ) continue;
		const float lz = FIXED2FLOAT( light->z );
		// [rc4l] DONTLIGHTSELF decides whether GL or the backend is the one in the wrong when an
		// item carrying its own glow comes out unlit. gl_SetDynSpriteLight skips a light whose
		// target is the very actor being lit when the flag is set, so an armor bonus standing in a
		// green pool of its OWN making is supposed to stay dark -- and a renderer that lights it is
		// the one that needs fixing, not the one that does not.
		Printf( "light %d: pos %.1f, %.1f, %.1f  radius %.0f  rgb %d,%d,%d%s  dz-from-floor %+.2f  "
				"owner %s%s%s",
			n, FIXED2FLOAT( light->x ), FIXED2FLOAT( light->y ), lz, radius,
			light->GetRed( ), light->GetGreen( ), light->GetBlue( ),
			light->IsSubtractive( ) ? " SUBTRACTIVE" : "",
			lz - floorz,
			( light->target != NULL ) ? light->target->GetClass( )->TypeName.GetChars( ) : "none",
			( light->flags4 & MF4_DONTLIGHTSELF ) ? " DONTLIGHTSELF" : "",
			( fabsf( lz - floorz ) < 1.f ) ? "   <-- ON THE FLOOR PLANE\n" : "\n" );

		// [rc4l] What GL DECIDES about this light, on the floor the player is standing on.
		//
		// gl_flats.cpp drops a light when the plane, evaluated at the LIGHT's x and y, is above it:
		// that one comparison decides whether a whole surface is lit or not lit, and there is no
		// middle. So when GL lights a floor and the port does not, the question is never "how much"
		// but "which side of this number did each of them land on" -- and reading the number off a
		// screenshot means bisecting a light's height by hand, ten captures at a time. It is right
		// here, in the engine, exactly as gl_flats.cpp computes it.
		if ( players[consoleplayer].mo != NULL && players[consoleplayer].mo->Sector != NULL )
		{
			const sector_t *sec = players[consoleplayer].mo->Sector;
			const float ph = FIXED2FLOAT( sec->rc.floorplane.ZatPoint( light->x, light->y ) );
			Printf( "          floor plane under the light %.3f vs light z %.3f -> GL %s\n",
				ph, lz, ( ph > lz ) ? "DROPS it" : "keeps it" );
		}
		n++;
	}
	Printf( "fua_dg_lights: %d active, player floor %.1f\n", n, floorz );
}
// [rc4l] The lights as last collected: the links each one touches and what they were collected under.
struct LightCollection
{
	std::vector<const std::vector<unsigned> *> touched;
	zx::hwrender::LightsStamp stamp;
};
static LightCollection g_collected = { {}, { -1, -1, -1 } };

// [rc4l] Every active dynamic light the render copy holds, collected into the shared storage buffer once per tic (see fuaDynLight in the shader for why there is no per-surface light list).
void CollectDynamicLights(Diligent::IDeviceContext *ctx)
{
	std::vector<const std::vector<unsigned> *> &touched = g_collected.touched;
	// [rc4l] Once per fill: lights change only in the ticker, and every pass of a frame (mirrors, stacked views, sky, camera textures) reads the same ones.
	const int flags = (fua_dg_dynlights ? 1 : 0) | (fua_dg_lightlinks ? 2 : 0);
	bool newLevel = false;
	if (!zx::hwrender::ComputeLightsDue(g_collected.stamp, zx::gamecopy::CopyTic(), zx::levelmesh::LevelGeneration(), flags, newLevel)) return;
	g_lightCount = 0;
	touched.clear();
	if (fua_dg_dynlights && g_lightBuf)
	{
		// [rc4l] The copy's light records, with the filters gl_GetLight applies: dormant lights are skipped, and a zero radius means off.
		g_lightData.Clear();
		int count;
		const zx::gamecopy::LightRecord *const lights = zx::gamecopy::Lights(count);
		for (int i = 0; i < count; i++)
		{
			const zx::gamecopy::LightRecord &light = lights[i];
			if (!light.active) continue;
			const float radius = light.radius * r_lights_size;
			if (radius <= 0.f) continue;
			if ((unsigned)g_lightCount >= g_lightBufCapacity / 2) break;

			float rgb[3];
			zx::hwrender::ComputeDynLightColor(light.red, light.green, light.blue, r_lights_additive || light.additive, r_lights_intensity, rgb);

			// Same axis swap the mesh uses: (x, z, y).
			g_lightData.Push(FIXED2FLOAT(light.x));
			g_lightData.Push(FIXED2FLOAT(light.z));
			g_lightData.Push(FIXED2FLOAT(light.y));
			g_lightData.Push(radius);
			g_lightData.Push(rgb[0]);
			g_lightData.Push(rgb[1]);
			g_lightData.Push(rgb[2]);
			g_lightData.Push(light.subtractive ? 1.0f : 0.0f);
			touched.push_back(&light.links);
			g_lightCount++;
		}
		if (g_lightCount > 0)
		{
			ctx->UpdateBuffer(g_lightBuf, 0, (Diligent::Uint64)g_lightData.Size() * 4, &g_lightData[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			static std::vector<zx::hwrender::LightTouchRange> ranges;
			static std::vector<unsigned> ids;
			zx::hwrender::ComputeLightTouchLists(touched, kLightTouchIdCapacity, ranges, ids);
			if (!fua_dg_lightlinks)
				for (zx::hwrender::LightTouchRange &r : ranges) r.count = zx::hwrender::kLightTouchesAll;
			if (g_lightTouchRangeBuf && g_lightTouchIdBuf)
			{
				ctx->UpdateBuffer(g_lightTouchRangeBuf, 0, (Diligent::Uint64)ranges.size() * sizeof(ranges[0]), &ranges[0],
					Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				if (!ids.empty())
					ctx->UpdateBuffer(g_lightTouchIdBuf, 0, (Diligent::Uint64)ids.size() * sizeof(unsigned), &ids[0],
						Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			}
		}
	}
}

// [rc4l] Bin this frame's lights into the cluster grid.
//
// One pass to count what lands in each cell, a prefix sum, and a second pass to fill the runs --
// the standard way, and worth stating why it is not simply a list per cell: a cell-of-vectors
// allocates thousands of times a frame and hands the GPU a pointer chase. Two flat buffers is what
// the shader can read and what a compute shader will produce later without changing the format.
//
// The grid math is in lightcluster_compute.h, tested off-engine. What is here is only the
// bookkeeping, deliberately: the boundaries are where the faults live, and they should not be in a
// file that needs a level loaded to run.
void BuildLightClusters(Diligent::IDeviceContext *ctx)
{
	g_clusterCells = 0;
	g_clusterRefs = 0;
	g_clusterTruncated = 0;
	if (!fua_dg_clusters || !g_clusterBuf || !g_lightIndexBuf || g_lightCount <= 0) return;

	if (GetSwapChain() == NULL) return;

	// [rc4l] The slice range is the CLUSTER far plane, not the projection's.
	//
	// BuildMVP's far plane is 65536 so that nothing in a Doom map is ever clipped, and slicing that
	// range would spend every slice on distance no light reaches. Anything beyond this falls in the
	// last slice, which is correct rather than approximate: a light 8000 units away is not lighting
	// you either way.
	const zx::hwrender::ClusterGrid grid =
		zx::hwrender::ComputeGridForScreen(PassWidth(), PassHeight(),
			zx::hwrender::kClusterZNear, zx::hwrender::kClusterZFar);
	const int cells = zx::hwrender::ComputeClusterCount(grid);
	if (cells <= 0 || (unsigned)cells > g_clusterCapacity) return;

	static TArray<unsigned int> counts, offsets, table, indices;
	counts.Resize((unsigned)cells);
	for (int i = 0; i < cells; i++) counts[i] = 0;

	// [rc4l] Pass one: how many lights land in each cell -- and keep the ranges.
	//
	// Pass two needs exactly the same answer, and recomputing it means projecting every light's box
	// through the matrix twice a frame for nothing. Eight corners times four thousand lights is not
	// free, and it was measurable: the whole binning pass is now the cost that grows with light
	// count, so the half of it that is pure repetition goes first.
	static TArray<zx::hwrender::ClusterRange> ranges;
	ranges.Resize((unsigned)g_lightCount);
	for (int l = 0; l < g_lightCount; l++)
	{
		const float *lp = &g_lightData[l * 8];
		const zx::hwrender::ClusterRange r =
			zx::hwrender::ComputeLightClustersFromMVP(grid, SceneMVP(), lp, lp[3]);
		ranges[l] = r;
		if (r.empty) continue;
		for (int z = r.z0; z <= r.z1; z++)
			for (int y = r.y0; y <= r.y1; y++)
				for (int x = r.x0; x <= r.x1; x++)
					counts[zx::hwrender::ComputeClusterIndex(grid, x, y, z)]++;
	}

	// [rc4l] A ceiling per CELL, not a ceiling on the buffer.
	//
	// Bounding the total is what a buffer needs; bounding each cell is what a picture needs. Left to
	// run out of buffer, the cells that happen to come last lose everything -- black rectangles in
	// whatever part of the world sorted late -- and 4096 lights packed round the camera made 85% of
	// the frame disagree that way. Capping each cell instead drops only the excess in the cells that
	// are genuinely overloaded, and 3840 cells at 256 apiece cannot overrun a million entries, so the
	// buffer limit stops being reachable at all.
	//
	// 256 lights reaching one cell is already far past anything a map does; the packed stress field
	// is the only thing that has ever hit it.
	enum { kMaxLightsPerCell = 256 };
	for (int i = 0; i < cells; i++)
		if (counts[i] > (unsigned)kMaxLightsPerCell) { counts[i] = kMaxLightsPerCell; g_clusterTruncated++; }

	// The prefix sum, and the ceiling that keeps a pathological frame from running off the buffer.
	offsets.Resize((unsigned)cells);
	unsigned int total = 0;
	for (int i = 0; i < cells; i++)
	{
		offsets[i] = total;
		// [rc4l] Overflow TRUNCATES this cell and says so. It does not empty it.
		//
		// Emptying a cell removes every light from a patch of the world at once, which is a black
		// rectangle rather than a dim one -- and when this first ran it dropped enough cells to make
		// 85% of the frame disagree with the brute-force path, reported by nothing louder than a
		// number in a stats line. Keeping what fits degrades the picture gently; the flag is what
		// makes the degradation visible, because a renderer that silently draws something else is
		// worse than one that stops.
		if (total + counts[i] > g_lightIndexCapacity)
		{
			counts[i] = (g_lightIndexCapacity > total) ? (g_lightIndexCapacity - total) : 0;
			g_clusterTruncated++;
		}
		total += counts[i];
	}

	// Pass two: write the runs. `filled` walks each cell's slot as entries arrive.
	static TArray<unsigned int> filled;
	filled.Resize((unsigned)cells);
	for (int i = 0; i < cells; i++) filled[i] = 0;
	indices.Resize(total > 0 ? total : 1);
	for (int l = 0; l < g_lightCount; l++)
	{
		const zx::hwrender::ClusterRange &r = ranges[l];
		if (r.empty) continue;
		for (int z = r.z0; z <= r.z1; z++)
			for (int y = r.y0; y <= r.y1; y++)
				for (int x = r.x0; x <= r.x1; x++)
				{
					const int cell = zx::hwrender::ComputeClusterIndex(grid, x, y, z);
					if (filled[cell] >= counts[cell]) continue;   // the dropped cells above
					indices[offsets[cell] + filled[cell]] = (unsigned int)l;
					filled[cell]++;
				}
	}

	table.Resize((unsigned)cells * 2);
	for (int i = 0; i < cells; i++) { table[i * 2] = offsets[i]; table[i * 2 + 1] = counts[i]; }

	ctx->UpdateBuffer(g_clusterBuf, 0, (Diligent::Uint64)table.Size() * 4, &table[0],
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	if (total > 0)
		ctx->UpdateBuffer(g_lightIndexBuf, 0, (Diligent::Uint64)total * 4, &indices[0],
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	g_clusterCells = cells;
	g_clusterRefs = (int)total;
}

}} // namespace zx::hwrender
