// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Line mirrors, raster and ray-traced, drawn into pictures of their own; reflective planes and stacked sectors are in dgviewtargets.cpp.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/gamecopy/level.h"   // [rc4l] the level as the render copy holds it
#include "features/hwrender/diligent/dgscene_glsl.h"   // [rc4l] the scene's light model, shared with the mirror pass
#include "features/viewsetup/viewsetup.h"   // [rc4l] FixedColormapShadeFor
#include "features/hwrender/computation/linemirror_compute.h"   // [rc4l] the reflected eye and how deep mirrors nest
#include "features/hwrender/computation/growbuffer_compute.h"   // [rc4l] ComputeGrownCapacity
#include "features/telemetry/telemetry.h"

EXTERN_CVAR(Bool, r_lights_checkside)

EXTERN_CVAR(Float, r_mask_sprite_threshold)
EXTERN_CVAR(Int, r_mirror_recursions)

namespace zx { namespace hwrender {

// [rc4l] Ray-traced reflections: the acceleration structure over the baked level.
//
// Why this rather than another planar pass. The planar mirror renders the whole world again per
// mirror and can only show what a visibility list built for the PLAYER's camera contains -- so the
// level reflects and the actors do not, and a mirror facing a mirror is impossible. A ray query
// needs no visibility list at all: it asks the structure what is along a direction. Recursion is
// free, the cost is proportional to the mirror's pixels rather than a full screen, and the static
// level is already one flat vertex buffer, which is exactly what an acceleration structure wants.
//
// Built once per level, from the same buffer the raster path draws. If the geometry moves (a door)
// the structure is rebuilt, which is rare and cheap next to the frame it happens on.
static Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS> g_blas;
static Diligent::RefCntAutoPtr<Diligent::ITopLevelAS>    g_tlas;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_asScratch;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_tlasInstances;
// [rc4l] The line mirrors' faces, a second instance (custom index 1) a traced ray reflects off again, and each mirror's plane, two triangles a mirror.
static Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS> g_mirrorBlas;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_mirrorASVB;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_mirrorPlanes;
static bool g_asBuilt = false;
static int  g_asVerts = 0;
// [rc4l] The mirrors a structure was built with, so a new level's are built in.
static int  g_asMirrors = -1;
static bool g_asMirrorFaces = false;   // [rc4l] the mirrors' faces made it into a structure of their own
struct MirrorSurface
{
	float corners[4][3];  // [rc4l] the whole line's face in mesh space (x, z-up, y), bottom and top at v1 then top and bottom at v2, for the visibility test
	float nx, ny, d;      // [rc4l] plane in map space (nx*x + ny*y = d), for reflecting the camera; mesh space's normal is (nx, 0, ny), which the ray query and GL's sheen reflect about
	int line;             // [rc4l] its linedef, which the sprites, models and decals GL's walk through it drew are filed under
};
static TArray<MirrorSurface> g_mirrors;
static TArray<float> g_mirrorVerts;
// [rc4l] The sprites a traced ray can meet, a box each (custom index 2) and their records for the shader, rebuilt every frame.
static Diligent::RefCntAutoPtr<Diligent::IBottomLevelAS> g_spriteBlas;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_spriteBoxes, g_spriteRecords, g_spriteScratch;
static unsigned g_spriteCapacity = 0, g_spriteCount = 0, g_spriteStepCapacity = 0;

bool RayTracingAvailable()
{
	auto *dev = GetDevice();
	return dev != NULL && dev->GetDeviceInfo().Features.RayTracing;
}

void ReleaseAccelerationStructures()
{
	g_tlas.Release();
	g_blas.Release();
	g_mirrorBlas.Release();
	g_mirrorASVB.Release();
	g_spriteBlas.Release();
	g_spriteBoxes.Release();
	g_spriteRecords.Release();
	g_spriteScratch.Release();
	g_spriteCapacity = g_spriteCount = g_spriteStepCapacity = 0;
	g_asMirrorFaces = false;
	g_mirrorPlanes.Release();
	g_asMirrors = -1;
	g_asScratch.Release();
	g_tlasInstances.Release();
	g_asBuilt = false;
	g_asVerts = 0;
	ReleaseMirrorBinding();   // the mirror SRB holds a reference to the TLAS
}

// [rc4l] The top level's instances, each's custom index its place here: the level, the mirrors' faces and this frame's sprites.
static const Diligent::Uint32 kTlasInstances = 3;

// [rc4l] A buffer the ray-tracing builds read or write: immutable with `data`, else default usage for a build to fill.
static bool CreateRayTracingBuffer(const char *name, Diligent::Uint64 size, const void *data, Diligent::RefCntAutoPtr<Diligent::IBuffer> &out)
{
	Diligent::BufferDesc bd;
	bd.Name = name;
	bd.Usage = data != NULL ? Diligent::USAGE_IMMUTABLE : Diligent::USAGE_DEFAULT;
	bd.BindFlags = Diligent::BIND_RAY_TRACING;
	bd.Size = size;
	Diligent::BufferData init(data, size);
	out.Release();
	CreateDeviceBuffer(bd, data != NULL ? &init : nullptr, &out);
	return out != nullptr;
}

// [rc4l] The top level over each of its instances that exists this frame.
static void BuildTopLevel(Diligent::IDeviceContext *ctx)
{
	const struct { const char *name; Diligent::IBottomLevelAS *blas; bool present; } parts[kTlasInstances] = {
		{ "level", g_blas, true }, { "mirrors", g_mirrorBlas, g_asMirrorFaces }, { "sprites", g_spriteBlas, g_spriteBlas && g_spriteCount > 0 },
	};
	Diligent::TLASBuildInstanceData inst[kTlasInstances];
	Diligent::Uint32 count = 0;
	for (Diligent::Uint32 i = 0; i < kTlasInstances; i++)
	{
		if (!parts[i].present) continue;
		inst[count].InstanceName = parts[i].name;
		inst[count].pBLAS = parts[i].blas;
		inst[count].Mask = 0xFF;
		inst[count++].CustomId = i;
	}
	Diligent::BuildTLASAttribs tlasAttribs;
	tlasAttribs.pTLAS                        = g_tlas;
	tlasAttribs.pInstances                   = inst;
	tlasAttribs.InstanceCount                = count;
	tlasAttribs.pInstanceBuffer              = g_tlasInstances;
	tlasAttribs.pScratchBuffer               = g_asScratch;
	tlasAttribs.TLASTransitionMode           = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.BLASTransitionMode           = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.InstanceBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.ScratchBufferTransitionMode  = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildTLAS(tlasAttribs);
}

// Build (or rebuild) the level's acceleration structure from the scene vertex buffer.
// [rc4l] The mirrors' bottom-level structure from their faces, and the plane buffer the shader reads a hit mirror's normal from; false when there are none.
static bool BuildMirrorStructure(Diligent::IRenderDevice *dev, Diligent::IDeviceContext *ctx, const TArray<float> &verts)
{
	const Diligent::Uint32 vertCount = verts.Size() / 6;
	if (vertCount < 3) return false;
	if (!CreateRayTracingBuffer("fua mirror AS VB", (Diligent::Uint64)verts.Size() * sizeof(float), &verts[0], g_mirrorASVB)) return false;
	// [rc4l] Each face's normal, and its mirror's line for a ray that bounces off it to find the view it is in after.
	const unsigned faces = verts.Size() / zx::hwrender::kMirrorFaceFloats;
	std::vector<int> lines(faces, -1);
	for (unsigned i = 0; i < faces && i < g_mirrors.Size(); i++) lines[i] = g_mirrors[i].line;
	std::vector<float> planes((size_t)faces * zx::hwrender::kMirrorPlaneFloats);
	zx::hwrender::ComputeMirrorPlaneRecords(&verts[0], faces, lines.data(), planes.data());
	if (!CreateStructuredBuffer("fua mirror planes", sizeof(float) * zx::hwrender::kMirrorPlaneFloats, faces, planes.data(), g_mirrorPlanes)) return false;

	Diligent::BLASTriangleDesc tri;
	tri.GeometryName = "mirrors";
	tri.MaxVertexCount = vertCount;
	tri.VertexValueType = Diligent::VT_FLOAT32;
	tri.VertexComponentCount = 3;
	tri.MaxPrimitiveCount = vertCount / 3;
	tri.IndexType = Diligent::VT_UNDEFINED;
	Diligent::BottomLevelASDesc desc;
	desc.Name = "fua mirror BLAS";
	desc.pTriangles = &tri;
	desc.TriangleCount = 1;
	dev->CreateBLAS(desc, &g_mirrorBlas);
	if (!g_mirrorBlas) return false;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> scratch;
	if (!CreateRayTracingBuffer("fua mirror AS scratch", g_mirrorBlas->GetScratchBufferSizes().Build, NULL, scratch)) return false;
	Diligent::BLASBuildTriangleData data;
	data.GeometryName = tri.GeometryName;
	data.pVertexBuffer = g_mirrorASVB;
	data.VertexStride = sizeof(float) * 6;
	data.VertexCount = vertCount;
	data.VertexValueType = tri.VertexValueType;
	data.VertexComponentCount = tri.VertexComponentCount;
	data.PrimitiveCount = vertCount / 3;
	data.Flags = Diligent::RAYTRACING_GEOMETRY_FLAG_OPAQUE;
	Diligent::BuildBLASAttribs attribs;
	attribs.pBLAS = g_mirrorBlas;
	attribs.pTriangleData = &data;
	attribs.TriangleDataCount = 1;
	attribs.pScratchBuffer = scratch;
	attribs.BLASTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	attribs.GeometryTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	attribs.ScratchBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildBLAS(attribs);
	return true;
}

bool EnsureAccelerationStructure()
{
	if (g_asBuilt && g_asVerts == (int)g_scene.cpuVerts.Size() && g_asMirrors == (int)g_mirrorVerts.Size()) return true;
	if (!RayTracingAvailable() || !g_scene.vb || g_scene.cpuVerts.Size() < 3) return false;

	auto *dev = GetDevice();
	auto *ctx = GetContext();
	ReleaseAccelerationStructures();

	const Diligent::Uint32 vertCount = (Diligent::Uint32)g_scene.cpuVerts.Size();
	const Diligent::Uint32 triCount  = vertCount / 3;

	Diligent::BLASTriangleDesc tri;
	tri.GeometryName         = "level";
	tri.MaxVertexCount       = vertCount;
	tri.VertexValueType      = Diligent::VT_FLOAT32;
	tri.VertexComponentCount = 3;
	tri.MaxPrimitiveCount    = triCount;
	tri.IndexType            = Diligent::VT_UNDEFINED;   // the mesh is already a triangle list

	Diligent::BottomLevelASDesc blasDesc;
	blasDesc.Name          = "fua level BLAS";
	blasDesc.pTriangles    = &tri;
	blasDesc.TriangleCount = 1;
	dev->CreateBLAS(blasDesc, &g_blas);
	if (!g_blas) return false;

	const Diligent::Uint64 blasScratch = g_blas->GetScratchBufferSizes().Build;
	if (!CreateRayTracingBuffer("fua AS scratch", blasScratch, NULL, g_asScratch)) return false;

	// The scene VB is interleaved: position is the first three floats of each SceneVertex, and the
	// stride carries the rest. No second copy of the level is needed.
	Diligent::BLASBuildTriangleData triData;
	triData.GeometryName         = tri.GeometryName;
	triData.pVertexBuffer        = g_scene.vb;
	triData.VertexOffset         = 0;
	triData.VertexStride         = sizeof(SceneVertex);
	triData.VertexCount          = vertCount;
	triData.VertexValueType      = tri.VertexValueType;
	triData.VertexComponentCount = tri.VertexComponentCount;
	triData.PrimitiveCount       = triCount;
	triData.Flags                = Diligent::RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	Diligent::BuildBLASAttribs blasAttribs;
	blasAttribs.pBLAS                     = g_blas;
	blasAttribs.pTriangleData             = &triData;
	blasAttribs.TriangleDataCount         = 1;
	blasAttribs.pScratchBuffer            = g_asScratch;
	blasAttribs.BLASTransitionMode        = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	blasAttribs.GeometryTransitionMode    = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	blasAttribs.ScratchBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildBLAS(blasAttribs);

	g_asMirrorFaces = BuildMirrorStructure(dev, ctx, g_mirrorVerts);
	Diligent::TopLevelASDesc tlasDesc;
	tlasDesc.Name             = "fua level TLAS";
	tlasDesc.MaxInstanceCount = kTlasInstances;
	tlasDesc.Flags            = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
	dev->CreateTLAS(tlasDesc, &g_tlas);
	if (!g_tlas) return false;

	if (!CreateRayTracingBuffer("fua TLAS instances", (Diligent::Uint64)Diligent::TLAS_INSTANCE_DATA_SIZE * kTlasInstances, NULL, g_tlasInstances)) return false;
	// [rc4l] One scratch serves both builds, so it is grown when the top level wants more than the level did.
	const Diligent::Uint64 tlasScratch = g_tlas->GetScratchBufferSizes().Build;
	if (tlasScratch > blasScratch && !CreateRayTracingBuffer("fua AS scratch", tlasScratch, NULL, g_asScratch)) return false;

	g_spriteCount = 0;
	BuildTopLevel(ctx);

	g_asBuilt = true;
	g_asVerts = (int)g_scene.cpuVerts.Size();
	g_asMirrors = (int)g_mirrorVerts.Size();
	Printf("vulkan: acceleration structure built -- %u triangles\n", (unsigned)triCount);
	return true;
}

// [rc4l] Room for `n` sprites and `steps` steps between mirror views, at least one sprite so the shader always has its buffer: boxes, records, their structure and its scratch, grown when short.
static bool EnsureSpriteBuffers(Diligent::IRenderDevice *dev, unsigned n, unsigned steps)
{
	const unsigned want = n > 0 ? n : 1;
	if (want <= g_spriteCapacity && steps <= g_spriteStepCapacity && g_spriteBlas) return true;
	const unsigned cap = ComputeGrownCapacity(want, 64), stepCap = ComputeGrownCapacity(steps, 16);
	g_spriteBlas.Release(); g_spriteBoxes.Release(); g_spriteRecords.Release(); g_spriteScratch.Release();
	g_spriteCapacity = g_spriteStepCapacity = 0;
	if (!CreateRayTracingBuffer("fua traced sprite boxes", (Diligent::Uint64)cap * 6 * sizeof(float), NULL, g_spriteBoxes)) return false;
	// [rc4l] Zeroed, so a ray reads no steps and no sprites before the first frame's are written.
	std::vector<float> zero(ComputeTracedSpriteHeaderFloats(stepCap) + cap * kTracedSpriteFloats, 0.f);
	if (!CreateStructuredBuffer("fua traced sprites", sizeof(float) * 4, zero.size() / 4, zero.data(), g_spriteRecords)) return false;
	Diligent::BLASBoundingBoxDesc box;
	box.GeometryName = "sprites";
	box.MaxBoxCount = cap;
	Diligent::BottomLevelASDesc desc;
	desc.Name = "fua traced sprite BLAS";
	desc.pBoxes = &box;
	desc.BoxCount = 1;
	desc.Flags = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_BUILD;
	dev->CreateBLAS(desc, &g_spriteBlas);
	if (!g_spriteBlas) return false;
	if (!CreateRayTracingBuffer("fua traced sprite scratch", g_spriteBlas->GetScratchBufferSizes().Build, NULL, g_spriteScratch)) return false;
	g_spriteCapacity = cap;
	g_spriteStepCapacity = stepCap;
	return true;
}

// [rc4l] This frame's sprites into their structure and the top level rebuilt over them, before any pass, since a build cannot run inside one.
static void UpdateTracedSprites(Diligent::IDeviceContext *ctx)
{
	auto *dev = GetDevice();
	if (!dev || !g_tlas || !g_blas) return;
	const TArray<TracedSprite> &sprites = g_scene.tracedSprites;
	const unsigned n = sprites.Size();
	const unsigned steps = (unsigned)(g_scene.tracedSteps.size() / 3);
	if (!EnsureSpriteBuffers(dev, n, steps)) { g_spriteCount = 0; return; }
	static TArray<float> boxes, records;
	const unsigned head = ComputeTracedSpriteHeaderFloats(steps);
	records.Resize(head + n * kTracedSpriteFloats);
	ComputeTracedSpriteHeader(steps > 0 ? &g_scene.tracedSteps[0] : NULL, steps, &records[0]);
	for (unsigned i = 0; i < n; i++) ComputeTracedSpriteRecord(sprites[i], &records[head + i * kTracedSpriteFloats]);
	UpdateBufferInPages(ctx, g_spriteRecords, 0, (size_t)records.Size() * sizeof(float), &records[0]);
	if (n > 0)
	{
		boxes.Resize(n * 6);
		for (unsigned i = 0; i < n; i++) ComputeTracedSpriteBox(sprites[i], &boxes[i * 6], &boxes[i * 6 + 3]);
		UpdateBufferInPages(ctx, g_spriteBoxes, 0, (size_t)boxes.Size() * sizeof(float), &boxes[0]);
		Diligent::BLASBuildBoundingBoxData data;
		data.GeometryName = "sprites";
		data.pBoxBuffer = g_spriteBoxes;
		data.BoxStride = 6 * sizeof(float);
		data.BoxCount = n;
		Diligent::BuildBLASAttribs attribs;
		attribs.pBLAS = g_spriteBlas;
		attribs.pBoxData = &data;
		attribs.BoxDataCount = 1;
		attribs.pScratchBuffer = g_spriteScratch;
		attribs.BLASTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
		attribs.GeometryTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
		attribs.ScratchBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
		ctx->BuildBLAS(attribs);
	}
	// [rc4l] Rebuilt even with none, so last frame's sprites leave the structure.
	if (n == 0 && g_spriteCount == 0) return;
	g_spriteCount = n;
	BuildTopLevel(ctx);
}

// [rc4l] Mirrors (Line_Mirror, special 182).
//
// A mirror is a portal, and the wall cache refuses any seg that produces one, so nothing is baked
// where a mirror is: the backend drew a hole and you saw the sky dome through it. The geometry is
// therefore built here, straight from the linedef, rather than expected from the mesh.
//
// The reflection is planar: put the camera through the mirror, render the world into a screen-sized
// target, then draw the mirror's quad sampling that target at the SAME screen position. That works
// because the reflected camera keeps the main camera's projection, so a point on the mirror surface
// lands on exactly the pixel showing what it reflects -- no second projection to get wrong, and no
// per-mirror texture coordinates.
// [rc4l] Every mirror's face as two triangles, in g_mirrors' order: g_mirrorVerts, declared with the structure that is built from it.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_mirrorVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_mirrorPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_mirrorSRB;
// [rc4l] Its traced path binds the level's materials and piece buffer on the pipeline, so a level change makes it again.
static void ReleaseMirrorLevels();
void ReleaseLineMirrorPipeline() { ReleaseMirrorLevels(); g_mirrorSRB.Release(); g_mirrorPSO.Release(); }
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_mirrorColor, g_mirrorDepth;
static int g_mirrorW = 0, g_mirrorH = 0;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_sheenCB;   // [rc4l] ComputeMirrorSheenConstants for the mirror being drawn
// True once the TLAS is genuinely bound to the mirror shader; false means use the planar path.
static bool g_mirrorTraced = false;

void CollectMirrors()
{
	g_mirrors.Clear();
	g_mirrorVerts.Clear();
	g_mirrorVB.Release();
	if (lines == NULL) return;

	for (int i = 0; i < numlines; i++)
	{
		const line_t *ln = &lines[i];
		// 182 is Line_Mirror (actionspecials.h). Named rather than included: that header is a macro
		// table that only expands correctly in the two files set up for it.
		if (ln->special != 182 || ln->frontsector == NULL) continue;

		const float x1 = FIXED2FLOAT(ln->v1->x), y1 = FIXED2FLOAT(ln->v1->y);
		const float x2 = FIXED2FLOAT(ln->v2->x), y2 = FIXED2FLOAT(ln->v2->y);
		const float dx = x2 - x1, dy = y2 - y1;
		const float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.001f) continue;

		MirrorSurface m;
		// [rc4l] The face from the linedef's own v1 to v2, floor to ceiling, as GLWall::Process takes a wall's ends from its line; mesh space is (x, z-up, y).
		const vertex_t *ends[4] = { ln->v1, ln->v1, ln->v2, ln->v2 };
		for (int k = 0; k < 4; k++)
		{
			const secplane_core_t &plane = (k == 0 || k == 3) ? ln->frontsector->rc.floorplane : ln->frontsector->rc.ceilingplane;
			m.corners[k][0] = FIXED2FLOAT(ends[k]->x);
			m.corners[k][1] = FIXED2FLOAT(plane.ZatPoint(ends[k]->x, ends[k]->y));
			m.corners[k][2] = FIXED2FLOAT(ends[k]->y);
		}
		m.nx = dy / len; m.ny = -dx / len;
		m.d = m.nx * x1 + m.ny * y1;
		m.line = i;
		// [rc4l] Split as GLWall::RenderWall fans it, bottom left to top right, since the sheen's coordinate is interpolated across each triangle; six floats a vertex, position then normal.
		for (int t = 0; t < 2; t++)
			for (int k = 0; k < 3; k++)
			{
				const float *p = m.corners[zx::levelmesh::ComputeFanTriangleVertex(4, t, k)];
				for (int q = 0; q < 3; q++) g_mirrorVerts.Push(p[q]);
				g_mirrorVerts.Push(m.nx); g_mirrorVerts.Push(0.f); g_mirrorVerts.Push(m.ny);
			}
		g_mirrors.Push(m);
	}
	if (g_mirrors.Size() > 0)
		Printf("vulkan: %d mirror surface(s)\n", (int)g_mirrors.Size());
}

// [rc4l] Re-scan for mirror lines after one has been created at runtime. See the fua_dg_mirrors
// CCMD, which lives outside this namespace and so cannot reach the statics above.
int RecollectMirrors()
{
	// [rc4l] The surface list only. The SRB is deliberately LEFT ALONE.
	//
	// It holds the acceleration structure, the vertex buffer, the sky and the material array --
	// nothing per-mirror -- so a new surface list does not invalidate it. Releasing it from here
	// took the process down: this runs from the console, which is not a safe moment to drop a
	// binding the next frame may already be using. CollectMirrors releases the mirror vertex
	// buffer, which IS per-surface, and that one is rebuilt on demand.
	CollectMirrors();
	return (int)g_mirrors.Size();
}

// [rc4l] The sheen block both mirror stages read, packed by ComputeMirrorSheenConstants.
#define FUA_MIRROR_SHEEN_BLOCK_GLSL \
	"layout(binding = 6) uniform Sheen { mat4 uSheenView; vec4 uSheenNormal; vec4 uSheenColor; vec4 uSheenLight; };\n"

// [rc4l] GL's sheen as RenderMirrorSurface draws it, for a mirror shader to add: mirror.png at the sphere-map coordinate, in the wall's light fogged to black, at uSheenColor.a.
#define FUA_MIRROR_SHEEN_PS_GLSL \
	FUA_SECTOR_LIGHT_GLSL \
	FUA_FIXED_COLORMAP_GLSL \
	FUA_MIRROR_SHEEN_BLOCK_GLSL \
	"layout(binding = 7) uniform texture2D uSheen; uniform sampler uSheen_sampler;\n" \
	"layout(location = 2) in vec2 vSheenUV;\n" \
	"vec3 fuaMirrorSheen() {\n" \
	"    vec4 texel = texture(sampler2D(uSheen, uSheen_sampler), vSheenUV);\n" \
	/* [rc4l] Under a fixed colormap GL draws the sheen through main.fp in white light, recoloured by the colormap alone. */ \
	"    if (uFixedColormap.w > 0.5) return fuaFixedColormap(texel.rgb) * (texel.a * uSheenColor.a);\n" \
	"    vec4 clip = uMVP * vec4(vWorld, 1.0);\n" \
	"    float fogfactor = fuaFogFactor(uSheenLight.y, uSheenLight.z, vec4(vWorld, clip.w), uCameraPos.xyz);\n" \
	"    vec3 light = fuaSectorLight(uSheenColor.rgb, uSheenLight.x, uSheenLight.y, fogfactor, gl_FragCoord.z);\n" \
	"    return texel.rgb * light * (texel.a * uSheenColor.a);\n" \
	"}\n"

static const char *kMirrorVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec3 aNormal;\n"
	FUA_CONSTANTS_GLSL
	FUA_MIRROR_SHEEN_BLOCK_GLSL
	"layout(location = 0) out vec3 vWorld;\n"
	"layout(location = 1) out vec3 vNormal;\n"
	"layout(location = 2) out vec2 vSheenUV;\n"
	"void main() {\n"
	"    vWorld = aPos;\n"
	"    vNormal = aNormal;\n"
	// [rc4l] ComputeSphereMapUV, per vertex as main.vp works it out, with the normal already turned and normalised on the CPU.
	"    vec3 u = normalize((uSheenView * vec4(aPos, 1.0)).xyz);\n"
	"    vec3 r = reflect(u, uSheenNormal.xyz);\n"
	"    float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));\n"
	"    vSheenUV = r.xy / m + 0.5;\n"
	"    gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"}\n";

// [rc4l] The reflection, traced rather than re-rendered.
//
// One ray per mirror pixel: reflect the eye ray about the surface and ask the acceleration structure
// what it hits. No visibility list is involved, so actors will appear without one being built for the
// mirror's camera; recursion becomes possible; and the work is proportional to the mirror's pixels
// rather than a full-screen scene pass per mirror.
//
// Shading the hit is the whole difficulty. A ray query returns a primitive index and barycentrics,
// not a shaded fragment, so the shader has to fetch that triangle itself. The scene vertex buffer is
// bound as storage and indexed directly: 19 floats per vertex, three vertices per primitive, with the
// baked per-vertex lighting at offset 5. That lighting is what gl_SetColor produced at bake time, so
// a reflection is lit by the engine's own numbers rather than by a second implementation of them.
//
// Not textured yet: a texture would need the material for that triangle, which means bindless. The
// lit flat colour is the honest intermediate -- it proves the fetch and the interpolation before the
// binding work that would hide a mistake in either.
// [rc4l] kNoMirrorView spelled for the traced shader, which steps a ray into no view when no walk went its way.
#define FUA_NO_MIRROR_VIEW_STR "-2"
static_assert(zx::levelmesh::kNoMirrorView == -2, "FUA_NO_MIRROR_VIEW_STR spells kNoMirrorView");

static const char *kMirrorPS =
	"#version 460\n"
	"#extension GL_EXT_ray_query : require\n"
	"#extension GL_EXT_nonuniform_qualifier : require\n"
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform texture2D uTex; uniform sampler uTex_sampler;\n"
	"layout(binding = 2) uniform accelerationStructureEXT uTLAS;\n"
	"layout(std430, binding = 3) readonly buffer Verts { float vtx[]; } VertsIn;\n"
	"layout(binding = 4) uniform texture2D uMaterials[FUA_MAT_SLOTS]; uniform sampler uMaterials_sampler;\n"
	"layout(binding = 5) uniform texture2D uSky; uniform sampler uSky_sampler;\n"
	"layout(std430, binding = 7) readonly buffer MirrorPlanes { vec4 mirrorPlanes[]; } MirrorPlanesIn;\n"
	// [rc4l] This frame's steps between mirror views (ComputeTracedSpriteHeader), then its sprites, four vec4s each (ComputeTracedSpriteRecord), the boxes of the third instance.
	"layout(std430, binding = 8) readonly buffer TracedSprites { vec4 tracedSprites[]; } TracedSpritesIn;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	FUA_MIRROR_SHEEN_PS_GLSL
	"layout(location = 0) out vec4 outColor;\n"
	// [rc4l] Six floats a vertex now: position, texture coordinate, and the index of the record
	// that holds everything else. The colour and the material this ray hits come from there.
	"const uint STRIDE = 6u;\n"
	FUA_PIECEDATA_GLSL
	"uint vertPiece(uint v) { return uint(VertsIn.vtx[v * STRIDE + 5u] + 0.5); }\n"
	"vec3 vertColor(uint v) { return PiecesIn.pieces[vertPiece(v)].colorLight.rgb; }\n"
	"vec2 vertUV(uint v)    { uint b = v * STRIDE + 3u; return vec2(VertsIn.vtx[b], VertsIn.vtx[b + 1u]); }\n"
	// [rc4l] ComputeTracedSpriteHit, line for line: where the ray from o along d meets sprite s's quad from either side, between tMin and tMax, and its texture coordinate there.
	"uint spriteAt(uint s) { return 1u + uint(TracedSpritesIn.tracedSprites[0].x) + s * 4u; }\n"
	"bool fuaSpriteHit(uint s, vec3 o, vec3 d, float tMin, float tMax, out float t, out vec2 uv) {\n"
	"    uint r = spriteAt(s);\n"
	"    vec3 p0 = TracedSpritesIn.tracedSprites[r].xyz, eu = TracedSpritesIn.tracedSprites[r + 1u].xyz, ev = TracedSpritesIn.tracedSprites[r + 2u].xyz;\n"
	"    vec4 tex = TracedSpritesIn.tracedSprites[r + 3u];\n"
	"    t = 0.0; uv = vec2(0.0);\n"
	"    vec3 n = cross(eu, ev);\n"
	"    float den = dot(d, n);\n"
	"    if (abs(den) < 1e-6) return false;\n"
	"    t = dot(p0 - o, n) / den;\n"
	"    if (t <= tMin || t >= tMax) return false;\n"
	"    vec3 w = o + d * t - p0;\n"
	"    float uu = dot(eu, eu), uv2 = dot(eu, ev), vv = dot(ev, ev), wu = dot(w, eu), wv = dot(w, ev);\n"
	"    float det = uu * vv - uv2 * uv2;\n"
	"    float a = (wu * vv - wv * uv2) / det, b = (wv * uu - wu * uv2) / det;\n"
	"    if (a < 0.0 || a > 1.0 || b < 0.0 || b > 1.0) return false;\n"
	"    uv = vec2(tex.x + (tex.y - tex.x) * a, tex.z + (tex.w - tex.z) * b);\n"
	"    return true;\n"
	"}\n"
	"uint spritePiece(uint s) { return uint(TracedSpritesIn.tracedSprites[spriteAt(s)].w + 0.5); }\n"
	"int spriteView(uint s)   { return int(TracedSpritesIn.tracedSprites[spriteAt(s) + 1u].w); }\n"
	// [rc4l] The view a ray in `view` is in after bouncing off line mirror `line`, or none when no walk went that way.
	"int fuaNextView(int view, int line) {\n"
	"    uint n = uint(TracedSpritesIn.tracedSprites[0].x);\n"
	"    for (uint i = 1u; i <= n; i++) { vec4 e = TracedSpritesIn.tracedSprites[i]; if (int(e.x) == view && int(e.y) == line) return int(e.z); }\n"
	"    return " FUA_NO_MIRROR_VIEW_STR ";\n"
	"}\n"
	// [rc4l] A piece's material slot, the whole array's worth: a mask left from a 512-slot array sent every slot past 511 to another texture.
	"uint pieceMat(uint p)    { return min(uint(PiecesIn.pieces[p].fogAlphaMat.w + 0.5), uint(FUA_MAT_SLOTS - 1)); }\n"
	"uint spriteMat(uint s)   { return pieceMat(spritePiece(s)); }\n"
	// [rc4l] A traced hit lit and fogged as GL's reflected view does it, at `dist` along the eye's ray through the mirrors, where it appears.
	"vec3 fuaTracedShade(vec3 texel, uint p, vec3 color, float dist) {\n"
	"    vec3 seen = uCameraPos.xyz + normalize(vWorld - uCameraPos.xyz) * dist;\n"
	"    vec4 clip = uMVP * vec4(seen, 1.0);\n"
	"    float fogMode = PiecesIn.pieces[p].fogAlphaMat.z;\n"
	"    float fogfactor = fuaFogFactor(fogMode, PiecesIn.pieces[p].fogAlphaMat.x, vec4(seen, clip.w), uCameraPos.xyz);\n"
	"    vec3 light = fuaSectorLight(color, PiecesIn.pieces[p].colorLight.w, fogMode, fogfactor, clip.z / clip.w);\n"
	"    return fuaColouredFog(texel * light, PiecesIn.pieces[p].fogColor.rgb, fogMode, fogfactor);\n"
	"}\n"
	"void main() {\n"
	"    vec3 sheen = fuaMirrorSheen();\n"
	// [rc4l] GL draws a mirror past r_mirror_recursions deep (uSheenLight.w) black under its sheen, ClearScreen's dim, and with 0 that is the first one.
	"    int levels = int(uSheenLight.w + 0.5);\n"
	"    if (levels < 1) { outColor = vec4(min(sheen, vec3(1.0)), 1.0); return; }\n"
	"    vec3 n = normalize(vNormal);\n"
	"    vec3 eye = normalize(vWorld - uCameraPos.xyz);\n"
	"    vec3 dir = reflect(eye, n);\n"
	// Nudged off the surface, or the mirror hits itself at t = 0.
	"    vec3 origin = vWorld + n * 0.5;\n"
	// [rc4l] Each mirror the ray passes on the way adds its sheen, as each of GL's nested mirror surfaces does.
	"    vec3 glaze = sheen;\n"
	// [rc4l] How far the eye is along the ray once it leaves this mirror, which every bounce adds to.
	"    float travelled = length(vWorld - uCameraPos.xyz);\n"
	"    rayQueryEXT rq;\n"
	"    uint hitSprite = 0u;\n"
	// [rc4l] The mirror view the ray is in, whose walk's sprites are the ones it can meet: the drawn mirror's first reflection, then each bounce's.
	"    int view = int(uSheenNormal.w);\n"
	"    vec2 hitUV = vec2(0.0);\n"
	"    for (int level = 1; ; level++) {\n"
	"        rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.1, dir, 20000.0);\n"
	// [rc4l] A sprite's box is only a candidate: its quad has to be met, nearer than what was met so far, on a texel the alpha test keeps.
	"        while (rayQueryProceedEXT(rq)) {\n"
	"            if (rayQueryGetIntersectionTypeEXT(rq, false) != gl_RayQueryCandidateIntersectionAABBEXT) continue;\n"
	"            uint s = uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, false));\n"
	"            if (spriteView(s) != view) continue;\n"
	"            float tMax = rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 20000.0 : rayQueryGetIntersectionTEXT(rq, true);\n"
	"            float st;\n"
	"            vec2 suv;\n"
	"            if (!fuaSpriteHit(s, origin, dir, 0.1, tMax, st, suv)) continue;\n"
	"            if (textureLod(sampler2D(uMaterials[nonuniformEXT(spriteMat(s))], uMaterials_sampler), suv, 0.0).a <= uFrame.w) continue;\n"
	"            hitSprite = s;\n"
	"            hitUV = suv;\n"
	"            rayQueryGenerateIntersectionEXT(rq, st);\n"
	"        }\n"
	"        if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) break;\n"
	"        if (rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true) != 1) break;\n"
	// [rc4l] Another mirror: GL renders it a level deeper, reflecting the view again, unless that is past its recursions.
	"        if (level >= levels) { outColor = vec4(min(glaze, vec3(1.0)), 1.0); return; }\n"
	"        vec4 mp = MirrorPlanesIn.mirrorPlanes[uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true)) / 2u];\n"
	"        vec3 m = mp.xyz;\n"
	"        view = fuaNextView(view, int(mp.w));\n"
	"        float hit = rayQueryGetIntersectionTEXT(rq, true);\n"
	"        travelled += hit;\n"
	"        origin = origin + dir * hit + m * 0.5;\n"
	"        dir = reflect(dir, m);\n"
	"        glaze += sheen;\n"
	"    }\n"
	// [rc4l] A miss means the ray left the level, and what is out there is the sky.
	//
	// Sky ceilings are portals and deliberately not mesh geometry, so upward rays SHOULD miss. Paint
	// that black and the reflection grows holes where the sky is; paint it the sky's average colour
	// and you get a flat patch that reads as the sky not being reflected at all. Sampling the sky
	// texture by ray direction, with the dome's own scale and rotation, is the only one of the three
	// that reflects a sky.
	"    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {\n"
	"        vec3 d = normalize(dir);\n"
	"        float el = asin(clamp(d.y, -1.0, 1.0));\n"
	"        float su = (atan(d.z, d.x) + uLightParams.y) / 6.28318531;\n"
	// The dome spans the horizon to 60 degrees over its texture, so map elevation the same way.
	"        float sv = clamp((1.0472 - el) / 1.0472, 0.0, 1.0);\n"
	"        outColor = vec4(min(texture(sampler2D(uSky, uSky_sampler), vec2(su * uScreen.z, sv * uScreen.w)).rgb + glaze, vec3(1.0)), 1.0);\n"
	"        return;\n"
	"    }\n"
	// [rc4l] A sprite: its texel in the light its own record carries, shaded as the level's surfaces are.
	"    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionGeneratedEXT) {\n"
	"        vec3 st = textureLod(sampler2D(uMaterials[nonuniformEXT(spriteMat(hitSprite))], uMaterials_sampler), hitUV, 0.0).rgb;\n"
	"        if (uFixedColormap.w > 0.5) { outColor = vec4(min(fuaFixedColormap(st) + glaze, vec3(1.0)), 1.0); return; }\n"
	"        uint sp = spritePiece(hitSprite);\n"
	"        vec3 sc = fuaTracedShade(st, sp, PiecesIn.pieces[sp].colorLight.rgb, travelled + rayQueryGetIntersectionTEXT(rq, true));\n"
	"        outColor = vec4(min(sc + glaze, vec3(1.0)), 1.0);\n"
	"        return;\n"
	"    }\n"
	"    uint prim = uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true));\n"
	"    vec2 bc = rayQueryGetIntersectionBarycentricsEXT(rq, true);\n"
	"    uint i0 = prim * 3u, i1 = i0 + 1u, i2 = i0 + 2u;\n"
	"    float w0 = 1.0 - bc.x - bc.y;\n"
	"    vec3 c = vertColor(i0) * w0 + vertColor(i1) * bc.x + vertColor(i2) * bc.y;\n"
	"    vec2 uv = vertUV(i0) * w0 + vertUV(i1) * bc.x + vertUV(i2) * bc.y;\n"
	// The material index rides in the vertex slot that used to hold a dynamic light index and has
	// been dead since the shader started testing every light.
	// [rc4l] The MATERIAL slot, shared with the raster path -- it used to be a batch index masked to
	// 127, which on any level with more than 128 batches reflected the wrong texture and could not
	// say so.
	"    uint mat = pieceMat(vertPiece(i0));\n"
	"    vec3 texel = texture(sampler2D(uMaterials[nonuniformEXT(mat)], uMaterials_sampler), uv).rgb;\n"
	// [rc4l] A fixed colormap recolours what the ray hit in white light, as the raster path does a level surface.
	"    if (uFixedColormap.w > 0.5) { outColor = vec4(min(fuaFixedColormap(texel) + glaze, vec3(1.0)), 1.0); return; }\n"
	"    c = fuaTracedShade(texel, vertPiece(i0), c, travelled + rayQueryGetIntersectionTEXT(rq, true));\n"
	"    outColor = vec4(min(c + glaze, vec3(1.0)), 1.0);\n"
	"}\n";

// [rc4l] The planar path's surface: the reflection RenderMirrors rendered from the mirrored eye, by screen position, with no ray query so an adapter without one can still draw it.
// [rc4l] Flipped left to right, since the mirrored eye sees the room the right way round and a mirror shows it reversed.
// [rc4l] GL's sheen goes on top, clamped as its additive blend into the framebuffer clamps.
static const char *kMirrorPlanarPS =
	"#version 450\n"
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform texture2D uTex; uniform sampler uTex_sampler;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	FUA_MIRROR_SHEEN_PS_GLSL
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    vec3 sheen = fuaMirrorSheen();\n"
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(sampler2D(uTex, uTex_sampler), 0));\n"
	"    outColor = vec4(min(texture(sampler2D(uTex, uTex_sampler), vec2(1.0 - uv.x, uv.y)).rgb + sheen, vec3(1.0)), 1.0);\n"
	"}\n";

// [rc4l] Which shader g_mirrorPSO was built with; the traced one only where the structure exists and fua_dg_rtmirrors asks for it.
static bool g_mirrorPSOTraced = false;
// [rc4l] The material slots the traced pipeline's array was last filled with, so a sprite's slot made since is bound before a ray meets it.
static unsigned g_mirrorMaterialSlots = 0;

// [rc4l] Every material the level uses, bound once on the PIPELINE.
//
// A rasterised draw binds one material and draws its batch; a ray can land on any triangle, so
// the material has to be selectable inside the shader. It does not change between draws, which
// makes it static -- and a static array is set on the PSO, where an SRB has no slot for it.
// Slots past the batch list take the white placeholder: an unbound element of a descriptor
// array is undefined, not merely unused.
// [rc4l] EVERY slot bound, without exception.
//
// A descriptor array with a hole in it is not "mostly bound". Diligent copies a pipeline's
// static resources into a binding and takes the process down if any element is missing, with
// nothing in the log. So the white placeholder fills everything past the batch list AND
// anything whose own upload failed, and if even the placeholder is unavailable the traced
// path is abandoned rather than half-built.
static bool BindMirrorMaterials(unsigned from, bool report)
{
	auto *v = g_mirrorPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "uMaterials");
	Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
	if (v == nullptr || white == nullptr)
	{
		Printf("mirror: %s -- reflections stay untraced\n", v ? "no placeholder texture" : "uMaterials not found");
		return false;
	}
	unsigned fellBack = 0;
	// [rc4l] A rebind replaces only the white placeholders under the new slots, which Diligent keeps unless told to overwrite.
	const Diligent::Uint32 to = zx::hwrender::ComputeMaterialBindEnd(from, MaterialSlotCapacity(), MaterialSlots().Size());
	const Diligent::SET_SHADER_RESOURCE_FLAGS flags = from == 0 ? Diligent::SET_SHADER_RESOURCE_FLAG_NONE : Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE;
	for (Diligent::Uint32 mi = from; mi < to; mi++)
	{
		const void *mat = (mi < MaterialSlots().Size()) ? MaterialSlots()[mi].material : NULL;
		const int tr = (mi < MaterialSlots().Size()) ? MaterialSlots()[mi].translation : 0;
		Diligent::IDeviceObject *obj = GetMaterialSRV(mat, tr);
		if (obj == nullptr) obj = white;
		if (mi < MaterialSlots().Size() && mat != NULL && obj == white) fellBack++;
		v->SetArray(&obj, mi, 1, flags);
	}
	g_mirrorMaterialSlots = MaterialSlots().Size();
	// [rc4l] Count how many came back as the white placeholder. A reflection showing flat white
	// surfaces looks the same whether the texture failed to upload or the index is wrong, and
	// this separates the two without reading it off a screenshot.
	if (report) Printf("mirror: %u materials bound, %u fell back to white\n", (unsigned)MaterialSlots().Size(), fellBack);
	return true;
}

// [rc4l] Whether materials were made since the traced pipeline's array was filled, which a ray meeting one would sample as white.
static bool MirrorMaterialsStale()
{
	return g_mirrorPSO && g_mirrorPSOTraced && MaterialSlots().Size() > g_mirrorMaterialSlots;
}

// [rc4l] The sprites' records on the traced binding, whose buffer a growth may have replaced.
static void BindTracedSprites()
{
	if (g_spriteRecords && g_mirrorSRB)
		if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "TracedSprites"))
			v->Set(g_spriteRecords->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
}

// [rc4l] The deepest picture level, one past r_mirror_recursions' ceiling of 10, whose mirrors are cleared.
static const int kMirrorLevels = 12;
static void ReleaseMirrorLevels();

// [rc4l] A binding of the mirror pipeline that paints from picture `color`, with GL's sheen texture beside it.
static bool BindMirrorPicture(Diligent::ITexture *color, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> &srb)
{
	srb.Release();
	// [rc4l] InitStaticResources = false. With true, Diligent copies the pipeline's static resources
	// into the binding at creation, and a static ARRAY with any element left unbound takes the
	// process down rather than reporting it.
	g_mirrorPSO->CreateShaderResourceBinding(&srb, false);
	if (!srb) return false;
	// Static resources are copied in explicitly, so a failure here is reported rather than being a
	// silent crash inside CreateShaderResourceBinding.
	g_mirrorPSO->InitializeStaticSRBResources(srb);
	if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	// [rc4l] glstuff/mirror.png, or the white placeholder so the binding is whole when GL has none and draws no sheen.
	if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSheen"))
	{
		const void *sheen = zx::materials::MaterialForTexture(zx::materials::MirrorSheenTexture());
		Diligent::IDeviceObject *srv = sheen ? GetMaterialSRV(sheen, 0) : NULL;
		v->Set(srv ? srv : GetMaterialSRV(NULL, 0));
	}
	return true;
}

bool EnsureMirrorResources()
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;
	const int w = FrameWidth(), h = FrameHeight();

	if (!g_mirrorColor || g_mirrorW != w || g_mirrorH != h)
	{
		g_mirrorColor.Release(); g_mirrorDepth.Release(); g_mirrorSRB.Release();
		if (!CreateViewTextures(w, h, "fua mirror", g_mirrorColor, g_mirrorDepth)) return false;
		g_mirrorW = w; g_mirrorH = h;
	}

	// [rc4l] A pipeline built for the other path is rebuilt, since the two shaders bind different resources.
	const bool wantTraced = g_tlas && fua_dg_rtmirrors && MaterialSlotCapacity() > 0;
	if (g_mirrorPSO && g_mirrorPSOTraced != wantTraced) { ReleaseMirrorLevels(); g_mirrorSRB.Release(); g_mirrorPSO.Release(); }
	if (!g_mirrorPSO)
	{
		Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
		g_mirrorPSOTraced = wantTraced;
		vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua mirror VS", kMirrorVS);
		ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua mirror PS", wantTraced ? kMirrorPS : kMirrorPlanarPS);
		if (!vs || !ps) return false;

		Diligent::LayoutElement layout[] = {
			Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
		};
		static Diligent::ShaderResourceVariableDesc vars[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ Diligent::SHADER_TYPE_PIXEL, "uTLAS", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ Diligent::SHADER_TYPE_PIXEL, "Verts", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ Diligent::SHADER_TYPE_PIXEL, "uSky", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ Diligent::SHADER_TYPE_PIXEL, "uSheen", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ Diligent::SHADER_TYPE_PIXEL, "MirrorPlanes", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			// [rc4l] Dynamic, since its buffer is replaced when the sprites outgrow it, and Diligent keeps a mutable binding's first resource.
			{ Diligent::SHADER_TYPE_PIXEL, "TracedSprites", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC },
		};
		Diligent::SamplerDesc samp;
		samp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
		samp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
		samp.MipFilter = Diligent::FILTER_TYPE_POINT;
		samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
		samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
		// [rc4l] The material array WRAPS; the reflection target does not.
		//
		// They shared one sampler, and it clamped. A wall's UVs stay near 0..1 so it looked right,
		// but a floor tiles its flat across the whole sector -- UVs run past 20 -- and clamping
		// smeared every one of them into a stretched edge colour. In a reflection that reads as an
		// untextured floor, which is indistinguishable from a missing texture until you notice the
		// walls beside it are fine.
		Diligent::SamplerDesc matSamp = samp;
		matSamp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
		matSamp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
		// [rc4l] What a ray meets is filtered as GL filters it, so a reflected sprite keeps its pixels instead of blurring, and a filter change rebuilds this pipeline with the scene's.
		FillSamplerFromEngine(matSamp);
		// [rc4l] The sheen is filtered as GL filters any material (CLAMP_NONE).
		Diligent::SamplerDesc sheenSamp = matSamp;

		const Diligent::ImmutableSamplerDesc samplers[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uSheen", sheenSamp },
			{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
			// [rc4l] The material array's sampler, uMaterials_sampler, is immutable as well, or it is a variable nothing binds.
			{ Diligent::SHADER_TYPE_PIXEL, "uMaterials", matSamp },
			{ Diligent::SHADER_TYPE_PIXEL, "uSky", samp },
		};

		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = "fua mirror PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		ClampDepthLikeGL(pci.GraphicsPipeline);
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
		pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
		pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
		pci.GraphicsPipeline.InputLayout.NumElements = 2;
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		// [rc4l] COUNTED FROM THE ARRAY, because a hand-written count silently retypes a variable.
		//
		// This said 3 while vars[] held 4. The fourth is uSky, so uSky was never registered as mutable
		// and fell through to DefaultVariableType, which is STATIC. A static variable is assigned on
		// the PSO; this code assigns uSky on the SRB, where GetVariableByName returns null for one --
		// so the assignment did nothing at all, quietly, and the binding went out with a hole in it.
		// Diligent then refused it with "No resource is assigned to static shader variable 'uSky'"
		// and the whole descriptor set was invalid, which is what made the material array next to it
		// sample white. That reads as bindless being broken, and bindless was fine: the same run
		// reports "41 materials bound, 0 fell back to white".
		pci.PSODesc.ResourceLayout.NumVariables = (Diligent::Uint32)(sizeof(vars) / sizeof(vars[0]));
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers =
			(Diligent::Uint32)(sizeof(samplers) / sizeof(samplers[0]));
		pci.pVS = vs; pci.pPS = ps;
		CreatePipeline(pci, &g_mirrorPSO);
		if (!g_mirrorPSO) { Printf("mirror: PSO failed\n"); return false; }
		// The traced path reads the same per-piece records the raster path does, so a reflection can
		// never disagree with the surface it reflects about what colour or texture that surface is.
		if (g_scene.pieceBuf)
			if (auto *v = g_mirrorPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Pieces"))
				v->Set(g_scene.pieceBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		BindSceneConstants(g_mirrorPSO);
		// [rc4l] The sheen block, made once like the scene constants, since a replaced buffer would leave this pipeline reading the old one.
		if (!g_sheenCB)
		{
			Diligent::BufferDesc cbd;
			cbd.Name = "fua mirror sheen";
			cbd.Size = sizeof(float) * zx::hwrender::kMirrorSheenFloats;
			cbd.Usage = Diligent::USAGE_DYNAMIC;
			cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
			cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
			CreateDeviceBuffer(cbd, nullptr, &g_sheenCB);
		}
		if (!g_sheenCB) { Printf("mirror: sheen buffer failed\n"); g_mirrorPSO.Release(); return false; }
		for (Diligent::SHADER_TYPE st : { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL })
			if (auto *v = g_mirrorPSO->GetStaticVariableByName(st, "Sheen")) v->Set(g_sheenCB);
		// [rc4l] Every material the level uses, on the pipeline (BindMirrorMaterials).
		if (wantTraced && !BindMirrorMaterials(0, true))
		{
			g_mirrorPSO.Release();
			return false;
		}
		g_mirrorSRB.Release();
	}

	// [rc4l] Slots made since the array was filled (a sprite's, first drawn this frame), bound and a fresh binding taken so it copies them.
	if (MirrorMaterialsStale())
	{
		BindMirrorMaterials(g_mirrorMaterialSlots, false);
		g_mirrorSRB.Release();
	}

	// [rc4l] Only CHECK for the structure here; never build it.
	//
	// BuildBLAS/BuildTLAS are not valid inside a render pass, and this runs mid-frame with render
	// targets bound -- calling them here killed the process outright with no message in the log. The
	// structure is built once at upload, where there is no pass open.
	if (!g_mirrorSRB)
	{
		if (!BindMirrorPicture(g_mirrorColor, g_mirrorSRB)) return false;
		// The traced path is only taken if the binding actually took. A variable the shader does not
		// declare, or a structure that failed to build, means falling back rather than committing an
		// SRB with a hole in it.
		g_mirrorTraced = false;
		if (g_mirrorPSOTraced)
		{
			if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTLAS"))
			{
				v->Set(g_tlas);
				g_mirrorTraced = true;
			}
			if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "Verts"))
			{
				v->Set(g_scene.vb->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
			}
			// [rc4l] The mirrors' planes, or the level's buffer standing in when there are no mirror faces to reflect off a second time.
			if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "MirrorPlanes"))
				v->Set((g_mirrorPlanes ? g_mirrorPlanes.RawPtr() : g_scene.vb.RawPtr())->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
			// [rc4l] The sprites' records, made now (zeroed) when no frame has made them yet.
			if (EnsureSpriteBuffers(GetDevice(), 0, 0)) BindTracedSprites();
			// [rc4l] The sky, resolved here rather than reused from EnsureSky: that runs mid-frame with
			// a render pass open, and this may have to upload the texture.
			if (auto *v2 = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSky"))
			{
				FTexture *rawSky = zx::gamecopy::Level().sky1.isValid() ? TexMan(zx::gamecopy::Level().sky1) : NULL;
				const void *skyMat = zx::materials::MaterialForTexture(rawSky);
				Diligent::IDeviceObject *srv = GetMaterialSRV(skyMat, 0);
				v2->Set(srv ? srv : GetMaterialSRV(NULL, 0));
			}

		}
	}

	if (!g_mirrorVB && g_mirrors.Size() > 0)
	{
		const TArray<float> &verts = g_mirrorVerts;
		Diligent::BufferDesc bd;
		bd.Name = "fua mirror VB";
		bd.Size = (Diligent::Uint64)verts.Size() * sizeof(float);
		bd.Usage = Diligent::USAGE_IMMUTABLE;
		bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		Diligent::BufferData bdata;
		bdata.pData = &verts[0];
		bdata.DataSize = bd.Size;
		CreateDeviceBuffer(bd, &bdata, &g_mirrorVB);
		if (!g_mirrorVB) return false;
	}
	return g_mirrors.Size() == 0 || g_mirrorVB.RawPtr() != nullptr;
}

void ReleaseMirrorBinding() { ReleaseMirrorLevels(); g_mirrorSRB.Release(); g_mirrorTraced = false; }

int PassWalkSlot()
{
	const bool mainCull = zx::levelmesh::ComputeMainCull(g_pass.mainView, g_pass.lineMirror, g_pass.inCameraPass, g_pass.clipPlane,
		zx::levelmesh::MainVisibility());
	return zx::levelmesh::ComputePassWalkSlot(g_scene.stackVisReady, g_pass.stackSlot, g_pass.inSkyPortal, zx::levelmesh::SkyboxWalked(), mainCull);
}

// [rc4l] The scene constant block for this frame's camera; passes differ only in clip plane, target size and bindless.
void WriteSceneConstants(float *cb, const float *clip, float targetW, float targetH, float materialSlots)
{
	zx::hwrender::SceneConstantInputs in;
	in.mvp = g_mvp;
	const float player[3] = { FIXED2FLOAT(viewx), FIXED2FLOAT(viewy), FIXED2FLOAT(viewz) };
	const float portal[3] = { g_pass.skyCamX, g_pass.skyCamY, g_pass.skyCamZ };
	float eye[3];
	zx::hwrender::ComputePassEye(g_pass.inSkyPortal, player, portal, eye);
	in.camX = eye[0]; in.camY = eye[1]; in.camZ = eye[2];
	in.lightMode = (float)(int)fua_dg_lightmode;
	in.lightCount = (float)LightCount();
	GetSkyConstants(in.skyAngle, in.skyXScale, in.skyVScale, in.skyCapR, in.skyCapG, in.skyCapB);
	in.clusters = ClusterCells() > 0;
	in.clip = clip;
	in.targetW = targetW; in.targetH = targetH;
	in.materialSlots = materialSlots;
	// [rc4l] The frame's own clock, which GL's shaders read too, so a warp or a fuzz is at the same moment in both.
	in.frameMS = (unsigned int)gl_frameMS;
	in.walkSlot = PassWalkSlot();
	in.playerPass = g_pass.mainView && g_pass.lineMirror < 0;
	in.skyHoles = g_scene.skyHoleCount;
	// [rc4l] AlphaFunc(GL_GEQUAL, t) keeps a texel only above t - 0.001, as FRenderState stores it.
	in.spriteAlphaCut = r_mask_sprite_threshold - 0.001f;
	// [rc4l] GL draws a camera texture with no fixed colormap (RenderTextureView), and every other view under the player's.
	in.fixedColormap = zx::viewsetup::FixedColormapShadeFor(g_pass.inCameraPass ? CM_DEFAULT : (int)gl_fixedcolormap);
	in.lightsCheckSide = r_lights_checkside;
	in.walkSlots = zx::levelmesh::WalkSlots();
	zx::hwrender::ComputeSceneConstants(in, cb);
}

// [rc4l] The sheen GL draws over this mirror (RenderMirrorSurface), in the light its walk captured, or none when it drew none.
static void WriteMirrorSheen(Diligent::IDeviceContext *ctx, const MirrorSurface &m, const float view[16])
{
	const zx::levelmesh::MeshPiece *shade = zx::levelmesh::MirrorSheen(m.line);
	const float rgb[3] = { shade ? shade->colorR : 0.f, shade ? shade->colorG : 0.f, shade ? shade->colorB : 0.f };
	Diligent::MapHelper<float> cb(ctx, g_sheenCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
	zx::hwrender::ComputeMirrorSheenConstants(view, m.nx, m.ny, rgb, shade ? shade->softLight : -1.f,
		shade ? (float)shade->fogMode : 0.f, shade ? shade->fogDensity : 0.f, shade != NULL, (int)r_mirror_recursions, m.line, &cb[0]);
}

// [rc4l] One mirror's face under the camera now, its sheen included: the traced path traces from it, the planar one samples the reflection just rendered.
static void DrawMirrorSurface(Diligent::IDeviceContext *ctx, unsigned index, const float view[16], Diligent::IShaderResourceBinding *srb)
{
	// [rc4l] No scene constants of its own: it is drawn inside the pass that shows it, whose block already holds that eye.
	WriteMirrorSheen(ctx, g_mirrors[index], view);
	if (g_mirrorTraced)
	{
		static bool said = false;
		if (!said) { said = true; Printf("vulkan: tracing mirror reflections\n"); }
	}
	Diligent::IBuffer *vbs[] = { g_mirrorVB };
	const Diligent::Uint64 offsets[] = { (Diligent::Uint64)index * 36 * sizeof(float) };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_mirrorPSO);
	ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs d;
	d.NumVertices = 6;
	d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(d);
}

// [rc4l] An eye a mirror pass looks from.
struct MirrorEye { fixed_t x, y, z; angle_t angle; };

// [rc4l] Runs `body` with the engine's view set to eye, and puts the engine's own back after.
template <class F> static void WithEye(const MirrorEye &e, F body)
{
	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	viewx = e.x; viewy = e.y; viewz = e.z; viewangle = e.angle;
	body();
	viewx = sx; viewy = sy; viewz = sz; viewangle = sa;
}

// [rc4l] The eye that sees mirror m's reflection of eye (ComputeMirrorEye), or false when eye is behind m.
static bool ReflectedEye(const MirrorSurface &m, const MirrorEye &eye, MirrorEye &out)
{
	float rx, ry;
	unsigned angle;
	if (!zx::hwrender::ComputeMirrorEye(m.nx, m.ny, m.d, FIXED2FLOAT(eye.x), FIXED2FLOAT(eye.y), eye.angle, rx, ry, angle)) return false;
	out.x = FLOAT2FIXED(rx); out.y = FLOAT2FIXED(ry); out.z = eye.z; out.angle = angle;
	return true;
}

// [rc4l] The pictures planar reflections are drawn into, by level and then by slot among one pass's mirrors, made the first time a mirror is seen that deep.
struct MirrorPicture
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> color, depth;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	int w = 0, h = 0;
};
static TArray<MirrorPicture> g_mirrorPool[kMirrorLevels];

static void ReleaseMirrorLevels()
{
	for (int i = 0; i < kMirrorLevels; i++) g_mirrorPool[i].Clear();
}

// [rc4l] Picture `slot` of level `level` at the window's size, or NULL past the deepest level.
static MirrorPicture *PoolPicture(int level, int slot)
{
	if (level < 1 || level >= kMirrorLevels || slot < 0) return NULL;
	TArray<MirrorPicture> &pool = g_mirrorPool[level];
	while ((int)pool.Size() <= slot) pool.Push(MirrorPicture());
	MirrorPicture &p = pool[slot];
	if (!p.color || !p.srb || p.w != g_mirrorW || p.h != g_mirrorH)
	{
		if (!CreateViewTextures(g_mirrorW, g_mirrorH, "fua mirror picture", p.color, p.depth) || !BindMirrorPicture(p.color, p.srb)) return NULL;
		p.w = g_mirrorW; p.h = g_mirrorH;
	}
	return &p;
}

// [rc4l] A picture ready to be painted from, handed over outside any render pass, for the reason RenderSkyPortal gives.
static void ReadyMirrorPicture(Diligent::IDeviceContext *ctx, Diligent::ITexture *color)
{
	Diligent::StateTransitionDesc barrier(color, Diligent::RESOURCE_STATE_UNKNOWN, Diligent::RESOURCE_STATE_SHADER_RESOURCE,
		Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
	ctx->TransitionResourceStates(1, &barrier);
}

// [rc4l] One mirror a pass shows: which, the pass showing it (-1 the player's view, else that node's reflection), how deep, the eye seeing it and the eye of its reflection, and the binding its surface paints from.
struct MirrorNode
{
	unsigned mirror;
	int parent, level;
	MirrorEye viewer, reflected;
	Diligent::IShaderResourceBinding *srb;
};
static TArray<MirrorNode> g_mirrorNodes;
// [rc4l] The node whose reflection is being drawn, -1 for the player's view.
static int g_mirrorPass = -1;

// [rc4l] Mirror i's reflection of `viewer` into picture `slot` of level `level`, the mirrors it shows drawn a level deeper first, so its pass paints them before its sprites as GL draws a mirror portal before the translucent pass; a mirror past r_mirror_recursions is GLPortal::ClearScreen's black.
static void RenderMirrorNode(Diligent::IDeviceContext *ctx, unsigned i, int level, int slot, const MirrorEye &viewer, int parent)
{
	const MirrorSurface &m = g_mirrors[i];
	MirrorEye reflected;
	MirrorPicture *pic = PoolPicture(level, slot);
	if (!ReflectedEye(m, viewer, reflected) || pic == NULL) return;
	const int node = (int)g_mirrorNodes.Size();
	const MirrorNode n = { i, parent, level, viewer, reflected, pic->srb };
	g_mirrorNodes.Push(n);
	if (!zx::hwrender::ComputeMirrorLevelDrawn(level, r_mirror_recursions))
	{
		auto *rtv = pic->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
		ctx->SetRenderTargets(1, &rtv, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		const float black[4] = { 0.f, 0.f, 0.f, 1.f };
		ctx->ClearRenderTarget(rtv, black, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ReadyMirrorPicture(ctx, pic->color);
		return;
	}
	WithEye(reflected, [] { BuildMVP(g_mvp); });
	int childSlot = 0;
	for (unsigned k = 0; k < g_mirrors.Size(); k++)
	{
		MirrorEye seen;
		if (k == i || !ReflectedEye(g_mirrors[k], reflected, seen) || !zx::horizon::ComputeQuadMayBeVisible(g_mvp, g_mirrors[k].corners)) continue;
		RenderMirrorNode(ctx, k, level + 1, childSlot++, reflected, node);
		WithEye(reflected, [] { BuildMVP(g_mvp); });
	}
	RenderViewSkyLooks(ctx, false, -1, true, g_mirrorW, g_mirrorH);
	// Clip to the mirror's own plane, in MESH space: x and z there are the map's x and y.
	g_pass.clipPlane[0] = m.nx; g_pass.clipPlane[1] = 0.f; g_pass.clipPlane[2] = m.ny;
	g_pass.clipPlane[3] = -m.d;
	if (g_pass.clipPlane[3] == 0.f) g_pass.clipPlane[3] = 0.0001f;   // w == 0 means "off"
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	BeginPassTargets(ctx, ViewPassTargets(pic->color, pic->depth, FrameViewNormal()), clear);
	// [rc4l] The view this reflection is, by its mirrors from the player outward, whose walk's sprites, models and decals it draws.
	static std::vector<int> path;
	zx::hwrender::ComputeRootPath(node, [](int n) { return g_mirrorNodes[n].parent; }, path);
	for (size_t k = 0; k < path.size(); k++) path[k] = g_mirrors[g_mirrorNodes[path[k]].mirror].line;
	const int outer = g_mirrorPass;
	g_mirrorPass = node;
	g_pass.lineMirror = m.line;
	g_pass.mirrorView = zx::levelmesh::MirrorViewOf(path.data(), (int)path.size());
	DrawWorld(ctx);
	ForgetViewSkyLooks();
	g_pass.mirrorView = -1;
	g_pass.lineMirror = -1;
	g_mirrorPass = outer;
	g_pass.clipPlane[0] = g_pass.clipPlane[1] = g_pass.clipPlane[2] = g_pass.clipPlane[3] = 0.f;
	ReadyMirrorPicture(ctx, pic->color);
}

void RenderMirrors(Diligent::IDeviceContext *ctx)
{
	ZX_ZONE("draw/mirrors");   // [rc4l] the line mirrors' pictures
	g_mirrorNodes.Clear();
	g_mirrorPass = -1;
	// [rc4l] A switch, so the pass can be BISECTED against the TDR.
	//
	// A planar mirror re-renders the world from the mirror's viewpoint, once per mirror. These maps
	// carry six, against fifty thousand pieces, and the crash under investigation is a GPU operation
	// running past the two-second timeout at a level transition -- so this pass is a suspect and
	// there was no way to take it out of the picture. Every other suspect has been ruled out by
	// switching it off and measuring; this one could not be, which is reason enough for the cvar.
	if (!fua_dg_mirrors_on) return;
	// [rc4l] GL draws a mirror's wall instead of its reflection with r_mirrors off, and the bake follows it (ComputeIsMirrorWall).
	if (!r_mirrors) return;
	if (g_mirrors.Size() == 0) return;
	// [rc4l] Also when a sprite's material came after the traced array was filled, so a ray meeting it samples it rather than white.
	if (!g_mirrorPSO || !g_mirrorSRB || !g_mirrorVB || MirrorMaterialsStale())
	{
		if (!EnsureMirrorResources()) return;
	}

	// [rc4l] With ray tracing there is no reflection pass at all: the mirror's own fragment shader
	// traces, so this collapses to drawing the surfaces. The planar path below stays for adapters
	// without ray tracing, where the alternative is no reflection at all.
	// [rc4l] Which path this build is taking, said once. The fallback is automatic -- an adapter
	// without ray tracing, or with the cvar off, gets the planar reflection instead of nothing -- and
	// a silent fallback is indistinguishable from a broken feature.
	const bool traced = g_mirrorTraced;
	if (traced)
	{
		UpdateTracedSprites(ctx);
		BindTracedSprites();
	}
	{
		static int said = -1;
		if (said != (int)traced)
		{
			said = (int)traced;
			Printf("vulkan mirrors: %s\n", traced ? "ray traced" : "planar (no ray tracing)");
		}
	}

	float savedMVP[16];
	for (int i = 0; i < 16; i++) savedMVP[i] = g_mvp[i];
	const MirrorEye player = { viewx, viewy, viewz, viewangle };
	int slot = 0;
	for (unsigned i = 0; i < g_mirrors.Size(); i++)
	{
		const MirrorSurface &m = g_mirrors[i];
		MirrorEye reflected;
		// [rc4l] A mirror you are BEHIND cannot be seen, so do not render the world into it.
		//
		// This loop re-rendered the entire scene once per mirror, unconditionally. These maps carry
		// six, so a frame was seven full scene renders over fifty thousand pieces -- and at a level
		// transition, with every material resolving for the first time and the batch layout not yet
		// settled, that is enough GPU work in one submit to cross the two-second timeout. Windows
		// resets the device and the driver terminates the process: the TDR that reads as a silent
		// crash after ~20 map loads. Measured, same build, same session, four sequences each:
		// mirrors on, 2 failures and 2 TDRs; mirrors off, 0 and 0.
		if (!ReflectedEye(m, player, reflected)) continue;
		if (traced)
		{
			const MirrorNode n = { i, -1, 1, player, reflected, g_mirrorSRB };
			g_mirrorNodes.Push(n);
			continue;
		}
		// [rc4l] ...and one nobody can see is not rendered either, since each costs a whole scene render and WDI07 holds four out of sight.
		if (!zx::levelmesh::ComputeRendersMirror(zx::levelmesh::MirrorWalkedThisFrame(m.line),
				zx::hwrender::StandaloneActive() && !zx::levelmesh::PortalViewsDerived(),
				zx::horizon::ComputeQuadMayBeVisible(savedMVP, m.corners)))
			continue;
		RenderMirrorNode(ctx, i, 1, slot++, player, -1);
		for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	}
	for (int i = 0; i < 16; i++) g_mvp[i] = savedMVP[i];
}

void DrawLineMirrorSurfaces(Diligent::IDeviceContext *ctx)
{
	// [rc4l] Only the pass whose mirrors they are: the player's view's in the main pass, a reflection's in its own.
	if (g_mirrorPass < 0 ? (!g_pass.mainView || g_pass.lineMirror >= 0) : g_pass.lineMirror != g_mirrors[g_mirrorNodes[g_mirrorPass].mirror].line) return;
	for (unsigned n = 0; n < g_mirrorNodes.Size(); n++)
	{
		const MirrorNode &node = g_mirrorNodes[n];
		if (node.parent != g_mirrorPass || node.srb == NULL) continue;
		// [rc4l] The sheen is reflected in the view of the eye that sees the mirror, as RenderMirrorSurface works it out.
		float view[16];
		WithEye(node.viewer, [&view] { zx::hwrender::ComputeBackendView(CurrentCamera(), view); });
		DrawMirrorSurface(ctx, node.mirror, view, node.srb);
		if (g_mirrorPass < 0) DrawMirrorFaceDecals(ctx, g_mirrors[node.mirror].line);
	}
}

}} // namespace zx::hwrender
