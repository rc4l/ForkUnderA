// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Views drawn into targets of their own: line mirrors, raster and ray-traced, reflective planes, and stacked sectors.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/diligent/dgscene_glsl.h"   // [rc4l] the scene's light model, shared with the mirror pass
#include "features/viewsetup/viewsetup.h"   // [rc4l] FixedColormapShadeFor
#include "features/hwrender/computation/linemirror_compute.h"   // [rc4l] the reflected eye and how deep mirrors nest

EXTERN_CVAR(Bool, r_lights_checkside)

EXTERN_CVAR(Float, r_mask_sprite_threshold)
EXTERN_CVAR(Int, r_mirror_recursions)

namespace zx { namespace hwrender {

// [rc4l] The pipeline that paints a stacked plane from its view.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_stackSurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_stackSurfVB;
static unsigned int g_stackSurfVBCapacity = 0;
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
static TArray<float> g_mirrorVerts;

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
	g_mirrorPlanes.Release();
	g_asMirrors = -1;
	g_asScratch.Release();
	g_tlasInstances.Release();
	g_asBuilt = false;
	g_asVerts = 0;
	ReleaseMirrorBinding();   // the mirror SRB holds a reference to the TLAS
}

// Build (or rebuild) the level's acceleration structure from the scene vertex buffer.
// [rc4l] The mirrors' bottom-level structure from their faces, and the plane buffer the shader reads a hit mirror's normal from; false when there are none.
static bool BuildMirrorStructure(Diligent::IRenderDevice *dev, Diligent::IDeviceContext *ctx, const TArray<float> &verts)
{
	const Diligent::Uint32 vertCount = verts.Size() / 6;
	if (vertCount < 3) return false;
	Diligent::BufferDesc vb;
	vb.Name = "fua mirror AS VB";
	vb.Usage = Diligent::USAGE_IMMUTABLE;
	vb.BindFlags = Diligent::BIND_RAY_TRACING;
	vb.Size = (Diligent::Uint64)verts.Size() * sizeof(float);
	Diligent::BufferData vbData(&verts[0], vb.Size);
	dev->CreateBuffer(vb, &vbData, &g_mirrorASVB);
	if (!g_mirrorASVB) return false;
	TArray<float> planes;
	for (Diligent::Uint32 v = 0; v < vertCount; v += 6)
	{
		const float *p = &verts[v * 6];
		planes.Push(p[3]); planes.Push(p[4]); planes.Push(p[5]); planes.Push(0.f);
	}
	Diligent::BufferDesc pb;
	pb.Name = "fua mirror planes";
	pb.Usage = Diligent::USAGE_IMMUTABLE;
	pb.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	pb.Mode = Diligent::BUFFER_MODE_STRUCTURED;
	pb.ElementByteStride = sizeof(float) * 4;
	pb.Size = (Diligent::Uint64)planes.Size() * sizeof(float);
	Diligent::BufferData pbData(&planes[0], pb.Size);
	dev->CreateBuffer(pb, &pbData, &g_mirrorPlanes);
	if (!g_mirrorPlanes) return false;

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
	Diligent::BufferDesc sb;
	sb.Name = "fua mirror AS scratch";
	sb.Usage = Diligent::USAGE_DEFAULT;
	sb.BindFlags = Diligent::BIND_RAY_TRACING;
	sb.Size = g_mirrorBlas->GetScratchBufferSizes().Build;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> scratch;
	dev->CreateBuffer(sb, nullptr, &scratch);
	if (!scratch) return false;
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

	Diligent::BufferDesc sb;
	sb.Name          = "fua AS scratch";
	sb.Usage         = Diligent::USAGE_DEFAULT;
	sb.BindFlags     = Diligent::BIND_RAY_TRACING;
	sb.Size          = g_blas->GetScratchBufferSizes().Build;
	dev->CreateBuffer(sb, nullptr, &g_asScratch);
	if (!g_asScratch) return false;

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

	const bool mirrors = BuildMirrorStructure(dev, ctx, g_mirrorVerts);
	Diligent::TopLevelASDesc tlasDesc;
	tlasDesc.Name             = "fua level TLAS";
	tlasDesc.MaxInstanceCount = 2;
	tlasDesc.Flags            = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
	dev->CreateTLAS(tlasDesc, &g_tlas);
	if (!g_tlas) return false;

	Diligent::BufferDesc ib;
	ib.Name      = "fua TLAS instances";
	ib.Usage     = Diligent::USAGE_DEFAULT;
	ib.BindFlags = Diligent::BIND_RAY_TRACING;
	ib.Size      = Diligent::TLAS_INSTANCE_DATA_SIZE * 2;
	dev->CreateBuffer(ib, nullptr, &g_tlasInstances);
	if (!g_tlasInstances) return false;

	Diligent::BufferDesc sb2 = sb;
	sb2.Size = g_tlas->GetScratchBufferSizes().Build;
	if (sb2.Size > sb.Size)
	{
		g_asScratch.Release();
		dev->CreateBuffer(sb2, nullptr, &g_asScratch);
		if (!g_asScratch) return false;
	}

	Diligent::TLASBuildInstanceData inst[2];
	inst[0].InstanceName = "level";
	inst[0].pBLAS        = g_blas;
	inst[0].Mask         = 0xFF;
	inst[0].CustomId     = 0;
	inst[1].InstanceName = "mirrors";
	inst[1].pBLAS        = g_mirrorBlas;
	inst[1].Mask         = 0xFF;
	inst[1].CustomId     = 1;

	Diligent::BuildTLASAttribs tlasAttribs;
	tlasAttribs.pTLAS                        = g_tlas;
	tlasAttribs.pInstances                   = inst;
	tlasAttribs.InstanceCount                = mirrors ? 2 : 1;
	tlasAttribs.pInstanceBuffer              = g_tlasInstances;
	tlasAttribs.pScratchBuffer               = g_asScratch;
	tlasAttribs.TLASTransitionMode           = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.BLASTransitionMode           = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.InstanceBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.ScratchBufferTransitionMode  = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildTLAS(tlasAttribs);

	g_asBuilt = true;
	g_asVerts = (int)g_scene.cpuVerts.Size();
	g_asMirrors = (int)g_mirrorVerts.Size();
	Printf("vulkan: acceleration structure built -- %u triangles\n", (unsigned)triCount);
	return true;
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
struct MirrorSurface
{
	float corners[4][3];  // [rc4l] the whole line's face in mesh space (x, z-up, y), bottom and top at v1 then top and bottom at v2, for the visibility test
	float nx, ny, d;      // [rc4l] plane in map space (nx*x + ny*y = d), for reflecting the camera; mesh space's normal is (nx, 0, ny), which the ray query and GL's sheen reflect about
	int line;             // [rc4l] its linedef, which the sprites, models and decals GL's walk through it drew are filed under
};
static TArray<MirrorSurface> g_mirrors;
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
			const secplane_t &plane = (k == 0 || k == 3) ? ln->frontsector->floorplane : ln->frontsector->ceilingplane;
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
	"layout(binding = 7) uniform sampler2D uSheen;\n" \
	"layout(location = 2) in vec2 vSheenUV;\n" \
	"vec3 fuaMirrorSheen() {\n" \
	"    vec4 texel = texture(uSheen, vSheenUV);\n" \
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
static const char *kMirrorPS =
	"#version 460\n"
	"#extension GL_EXT_ray_query : require\n"
	"#extension GL_EXT_nonuniform_qualifier : require\n"
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(binding = 2) uniform accelerationStructureEXT uTLAS;\n"
	"layout(std430, binding = 3) readonly buffer Verts { float vtx[]; };\n"
	"layout(binding = 4) uniform sampler2D uMaterials[FUA_MAT_SLOTS];\n"
	"layout(binding = 5) uniform sampler2D uSky;\n"
	"layout(std430, binding = 7) readonly buffer MirrorPlanes { vec4 mirrorPlanes[]; };\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	FUA_MIRROR_SHEEN_PS_GLSL
	"layout(location = 0) out vec4 outColor;\n"
	// [rc4l] Six floats a vertex now: position, texture coordinate, and the index of the record
	// that holds everything else. The colour and the material this ray hits come from there.
	"const uint STRIDE = 6u;\n"
	FUA_PIECEDATA_GLSL
	"uint vertPiece(uint v) { return uint(vtx[v * STRIDE + 5u] + 0.5); }\n"
	"vec3 vertColor(uint v) { return pieces[vertPiece(v)].colorLight.rgb; }\n"
	"vec2 vertUV(uint v)    { uint b = v * STRIDE + 3u; return vec2(vtx[b], vtx[b + 1u]); }\n"
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
	"    float travelled = 0.0;\n"
	"    rayQueryEXT rq;\n"
	"    for (int level = 1; ; level++) {\n"
	"        rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.1, dir, 20000.0);\n"
	"        while (rayQueryProceedEXT(rq)) {}\n"
	"        if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) break;\n"
	"        if (rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true) != 1) break;\n"
	// [rc4l] Another mirror: GL renders it a level deeper, reflecting the view again, unless that is past its recursions.
	"        if (level >= levels) { outColor = vec4(min(glaze, vec3(1.0)), 1.0); return; }\n"
	"        vec3 m = mirrorPlanes[uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true)) / 2u].xyz;\n"
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
	"        outColor = vec4(min(texture(uSky, vec2(su * uScreen.z, sv * uScreen.w)).rgb + glaze, vec3(1.0)), 1.0);\n"
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
	"    uint mat = uint(pieces[vertPiece(i0)].fogAlphaMat.w + 0.5) & 511u;\n"
	"    vec3 texel = texture(uMaterials[nonuniformEXT(mat)], uv).rgb;\n"
	// [rc4l] A fixed colormap recolours what the ray hit in white light, as the raster path does a level surface.
	"    if (uFixedColormap.w > 0.5) { outColor = vec4(min(fuaFixedColormap(texel) + glaze, vec3(1.0)), 1.0); return; }\n"
	"    c *= texel;\n"
	// Distance falls off the same way the raster path's fog does, so a far reflection reads as far.
	"    float t = travelled + rayQueryGetIntersectionTEXT(rq, true);\n"
	"    c *= clamp(1.0 - t / 4000.0, 0.15, 1.0);\n"
	"    outColor = vec4(min(c + glaze, vec3(1.0)), 1.0);\n"
	"}\n";

// [rc4l] The planar path's surface: the reflection RenderMirrors rendered from the mirrored eye, by screen position, with no ray query so an adapter without one can still draw it.
// [rc4l] Flipped left to right, since the mirrored eye sees the room the right way round and a mirror shows it reversed.
// [rc4l] GL's sheen goes on top, clamped as its additive blend into the framebuffer clamps.
static const char *kMirrorPlanarPS =
	"#version 450\n"
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	FUA_MIRROR_SHEEN_PS_GLSL
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    vec3 sheen = fuaMirrorSheen();\n"
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uTex, 0));\n"
	"    outColor = vec4(min(texture(uTex, vec2(1.0 - uv.x, uv.y)).rgb + sheen, vec3(1.0)), 1.0);\n"
	"}\n";

// [rc4l] Which shader g_mirrorPSO was built with; the traced one only where the structure exists and fua_dg_rtmirrors asks for it.
static bool g_mirrorPSOTraced = false;

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
	const bool wantTraced = g_tlas && fua_dg_rtmirrors;
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
		// [rc4l] The sheen is filtered as GL filters any material (CLAMP_NONE), and a filter change rebuilds this pipeline with the scene's.
		Diligent::SamplerDesc sheenSamp = matSamp;
		FillSamplerFromEngine(sheenSamp);

		const Diligent::ImmutableSamplerDesc samplers[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uSheen", sheenSamp },
			{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
			// [rc4l] uMaterials needs its own immutable sampler. A GLSL sampler2D is a COMBINED image
			// sampler, so an array of them needs a sampler declared for the array too -- without it the
			// binding is incomplete and the process dies inside CreateShaderResourceBinding with nothing
			// in the log, which looks like the array size or the feature rather than a missing sampler.
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
		dev->CreateGraphicsPipelineState(pci, &g_mirrorPSO);
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
			dev->CreateBuffer(cbd, nullptr, &g_sheenCB);
		}
		if (!g_sheenCB) { Printf("mirror: sheen buffer failed\n"); g_mirrorPSO.Release(); return false; }
		for (Diligent::SHADER_TYPE st : { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL })
			if (auto *v = g_mirrorPSO->GetStaticVariableByName(st, "Sheen")) v->Set(g_sheenCB);
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
		if (wantTraced)
		{
			auto *v = g_mirrorPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "uMaterials");
			Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
			if (v == nullptr || white == nullptr)
			{
				Printf("mirror: %s -- reflections stay untraced\n",
					v ? "no placeholder texture" : "uMaterials not found");
				g_mirrorPSO.Release();
				return false;
			}
			unsigned fellBack = 0;
			for (Diligent::Uint32 mi = 0; mi < (Diligent::Uint32)MaterialSlotCapacity(); mi++)
			{
				const void *mat = (mi < MaterialSlots().Size()) ? MaterialSlots()[mi].material : NULL;
				const int tr = (mi < MaterialSlots().Size()) ? MaterialSlots()[mi].translation : 0;
				Diligent::IDeviceObject *obj = GetMaterialSRV(mat, tr);
				if (obj == nullptr) obj = white;
				if (mi < MaterialSlots().Size() && mat != NULL && obj == white) fellBack++;
				v->SetArray(&obj, mi, 1);
			}
			// [rc4l] Count how many came back as the white placeholder. A reflection showing flat white
			// surfaces looks the same whether the texture failed to upload or the index is wrong, and
			// this separates the two without reading it off a screenshot.
			Printf("mirror: %u materials bound, %u fell back to white\n",
				(unsigned)MaterialSlots().Size(), fellBack);
		}
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
			// [rc4l] The sky, resolved here rather than reused from EnsureSky: that runs mid-frame with
			// a render pass open, and this may have to upload the texture.
			if (auto *v2 = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSky"))
			{
				FTexture *rawSky = sky1texture.isValid() ? TexMan(sky1texture) : NULL;
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
		dev->CreateBuffer(bd, &bdata, &g_mirrorVB);
		if (!g_mirrorVB) return false;
	}
	return g_mirrors.Size() == 0 || g_mirrorVB.RawPtr() != nullptr;
}

void ReleaseMirrorBinding() { ReleaseMirrorLevels(); g_mirrorSRB.Release(); g_mirrorTraced = false; }

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
	// [rc4l] The player's own pass, with no mirror plane, draws only what the visibility walk reached, as GL draws only what its walk reached.
	const bool mainCull = g_pass.mainView && g_pass.lineMirror < 0 && !g_pass.inCameraPass && g_pass.clipPlane[0] == 0.f &&
		g_pass.clipPlane[1] == 0.f && g_pass.clipPlane[2] == 0.f && g_pass.clipPlane[3] == 0.f && zx::levelmesh::MainVisibility();
	in.walkSlot = zx::levelmesh::ComputePassWalkSlot(g_scene.stackVisReady, g_pass.stackSlot, g_pass.inSkyPortal, zx::levelmesh::SkyboxWalked(), mainCull);
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
		shade ? (float)shade->fogMode : 0.f, shade ? shade->fogDensity : 0.f, shade != NULL, (int)r_mirror_recursions, &cb[0]);
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

// [rc4l] The world from another eye, at the window's size, and the binding that paints a surface from it: a reflective plane's reflection, a stacked view.
struct ViewTarget
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> color;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	int w, h;
	ViewTarget() : w(0), h(0) {}
	void Release() { srb.Release(); color.Release(); w = h = 0; }
};

// [rc4l] The depth every view target is rendered with, one view at a time, since a view's picture is all that outlives its render.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_viewDepth;

// [rc4l] Makes a target's picture at the window's size, and its binding on pso's texture variable, whenever it has none at that size.
static bool EnsureViewTarget(ViewTarget &t, Diligent::IPipelineState *pso, const char *var, const char *name)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !pso) return false;
	const int w = FrameWidth(), h = FrameHeight();
	if (t.color && t.srb && t.w == w && t.h == h) return true;
	t.Release();
	if (!CreateColorTarget(w, h, name, t.color) ||
		!CreatePictureBinding(pso, var, t.color, t.srb)) return false;
	t.w = w; t.h = h;
	return true;
}

// [rc4l] Every target of a pool made at the window's size at once, the pool `size` long, so no view in sight waits on one being made; false when one cannot be.
static bool FitViewPool(TArray<ViewTarget> &pool, unsigned size, Diligent::IPipelineState *pso, const char *var, const char *name)
{
	if (pool.Size() != size) pool.Resize(size);
	for (unsigned k = 0; k < size; k++)
		if (!EnsureViewTarget(pool[k], pso, var, name)) return false;
	return true;
}

// [rc4l] The shared view depth at a target's size, made when it has none at that size.
static Diligent::ITexture *ViewDepth(const ViewTarget &t)
{
	if (g_viewDepth)
	{
		const auto &d = g_viewDepth->GetDesc();
		if ((int)d.Width == t.w && (int)d.Height == t.h) return g_viewDepth;
	}
	if (!CreateDepthTarget(t.w, t.h, "fua view", g_viewDepth)) return NULL;
	return g_viewDepth;
}

// [rc4l] The world into a target, from the eye now in viewx/viewy/viewz/viewpitch and through g_pass.clipPlane.
static void RenderWorldInto(Diligent::IDeviceContext *ctx, ViewTarget &t)
{
	Diligent::ITexture *depth = ViewDepth(t);
	if (depth == NULL) return;
	BuildMVP(g_mvp);
	// [rc4l] A stacked view's other skies from its own walk's, a plane mirror's from the player's and the mirrors'.
	RenderViewSkyLooks(ctx, false, g_pass.stackSlot, g_pass.stackSlot < 0, t.w, t.h);
	auto *rtv = t.color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	auto *dsv = depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	ctx->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	DrawWorld(ctx);
	ForgetViewSkyLooks();
}

// [rc4l] A rendered target changes hands here, outside the frame's render pass, for the reason RenderSkyPortal gives.
static void HandOverViewTarget(Diligent::IDeviceContext *ctx, ViewTarget &t)
{
	Diligent::StateTransitionDesc barrier(t.color, Diligent::RESOURCE_STATE_UNKNOWN,
		Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
	ctx->TransitionResourceStates(1, &barrier);
}

// Render each mirror's reflection and draw its surface. Called after the world, before the 2D layer.
// [rc4l] Reflective floors and ceilings as GL's GLPlaneMirrorPortal draws them: each visible plane gets the world rendered from the mirrored eye into a slot before the frame, and the world pass paints its pieces from that picture, flipped, under the translucent floor (decisions in planemirror_compute).
// [rc4l] One target per distinct reflective plane the level has now, as GL has no limit, all made before any comes into view.
static TArray<ViewTarget> g_pm;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pmPSO;
// [rc4l] Reflections rendered this frame.
static int g_pmUsed = 0;

void ReleasePlaneMirrors()
{
	g_pm.Clear();
	g_pmPSO.Release();
	g_viewDepth.Release();
	g_pmUsed = 0;
}

// [rc4l] The plane-mirror pipeline, and a target for each of the level's `planes` reflective planes.
static bool EnsurePlaneMirrorResources(int planes)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !g_cb) return false;
	const auto &sd = swap->GetDesc();
	if (!g_pmPSO)
	{
		static const char *kPlaneMirrorVS =
			"#version 450\n"
			"layout(location = 0) in vec3 aPos;\n"
			"layout(location = 1) in vec2 aUV;\n"
			"layout(location = 2) in float aPiece;\n"
			FUA_CONSTANTS_GLSL
			"void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";
		// [rc4l] The mirrored eye sees the reflection upright, so the plane samples it upside down at its own screen position.
		static const char *kPlaneMirrorPS =
			"#version 450\n"
			"layout(binding = 1) uniform sampler2D uReflection;\n"
			"layout(location = 0) out vec4 outColor;\n"
			"void main() {\n"
			"    ivec2 size = textureSize(uReflection, 0);\n"
			"    ivec2 p = ivec2(gl_FragCoord.xy);\n"
			"    outColor = vec4(texelFetch(uReflection, ivec2(p.x, size.y - 1 - p.y), 0).rgb, 1.0);\n"
			"}\n";
		Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
		vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua plane mirror VS", kPlaneMirrorVS);
		ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua plane mirror PS", kPlaneMirrorPS);
		if (!vs || !ps) return false;
		static Diligent::ShaderResourceVariableDesc vars[] = {
			{ Diligent::SHADER_TYPE_VERTEX, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
			{ Diligent::SHADER_TYPE_PIXEL, "uReflection", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		Diligent::LayoutElement elems[] = {
			Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
			Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},
		};
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = "fua plane mirror PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		ClampDepthLikeGL(pci.GraphicsPipeline);
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = sd.ColorBufferFormat;
		pci.GraphicsPipeline.DSVFormat = sd.DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		// [rc4l] Tested so nearer walls hide it, never written, so the translucent floor drawn over it still passes on the same plane.
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
		pci.GraphicsPipeline.InputLayout.LayoutElements = elems;
		pci.GraphicsPipeline.InputLayout.NumElements = 3;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 2;
		// [rc4l] A combined image sampler needs a sampler in its descriptor even though texelFetch never uses it; without one the driver faults writing the binding.
		Diligent::SamplerDesc samp;
		samp.MinFilter = samp.MagFilter = Diligent::FILTER_TYPE_POINT;
		samp.MipFilter = Diligent::FILTER_TYPE_POINT;
		samp.AddressU = samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
		Diligent::ImmutableSamplerDesc samplers[] = { { Diligent::SHADER_TYPE_PIXEL, "uReflection", samp } };
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
		pci.pVS = vs;
		pci.pPS = ps;
		dev->CreateGraphicsPipelineState(pci, &g_pmPSO);
		if (!g_pmPSO) return false;
		BindSceneConstants(g_pmPSO);
	}
	return FitViewPool(g_pm, (unsigned)planes, g_pmPSO, "uReflection", "fua plane mirror");
}

// [rc4l] Before the frame: pick the reflective planes the eye faces and can see, and render each one's reflection.
void RenderPlaneMirrors(Diligent::IDeviceContext *ctx)
{
	g_pmUsed = 0;
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++) g_scene.batches[g_scene.blendBatches[i]].mirrorSlot = -1;
	if (fua_dg_planemirrors <= 0 || sectors == NULL || !g_scene.vb) return;

	float mvp[16];
	BuildMVP(mvp);
	const float vz = FIXED2FLOAT(viewz);
	// [rc4l] Every reflective plane the level has now, and of those the ones the eye faces and can see, each the slot it is rendered into.
	static TArray<zx::hwrender::MirrorPlane> planes, slots;
	planes.Resize(g_scene.blendBatches.Size());
	slots.Resize(g_scene.blendBatches.Size());
	int planeCount = 0;
	const int maxSlots = (int)planes.Size();
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
	{
		SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
		if (b.mirrorSector < 0 || b.mirrorSector >= numsectors || b.count == 0) continue;
		const sector_t &s = sectors[b.mirrorSector];
		zx::hwrender::MirrorPlane p;
		p.ceiling = b.mirrorCeiling;
		p.z = FIXED2FLOAT((b.mirrorCeiling ? s.ceilingplane : s.floorplane).ZatPoint(viewx, viewy));
		zx::hwrender::ComputePlaneMirrorSlot(&planes[0], planeCount, maxSlots, p);
		if (!zx::hwrender::ComputePlaneMirrorFaces(b.mirrorCeiling, p.z, vz)) continue;
		const float mn[3] = { b.minX, b.minY, b.minZ }, mx[3] = { b.maxX, b.maxY, b.maxZ };
		if (!zx::hwrender::ComputeBoxMayBeVisible(mvp, mn, mx)) continue;
		b.mirrorSlot = zx::hwrender::ComputePlaneMirrorSlot(&slots[0], g_pmUsed, maxSlots, p);
	}
	if (planeCount == 0) return;
	if (!EnsurePlaneMirrorResources(planeCount)) { g_pmUsed = 0; return; }
	if (g_pmUsed == 0) return;

	const fixed_t sz = viewz;
	const int sp = viewpitch;
	float savedMVP[16];
	for (int k = 0; k < 16; k++) savedMVP[k] = g_mvp[k];
	const bool savedGBuf = g_pass.gbufBound;
	g_pass.gbufBound = false;
	for (int k = 0; k < g_pmUsed; k++)
	{
		viewz = FLOAT2FIXED(zx::hwrender::ComputePlaneMirrorEyeZ(slots[k].z, vz));
		viewpitch = -sp;
		zx::hwrender::ComputePlaneMirrorClip(slots[k].ceiling, slots[k].z, g_pass.clipPlane);
		RenderWorldInto(ctx, g_pm[k]);
	}
	viewz = sz;
	viewpitch = sp;
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	g_pass.clipPlane[0] = g_pass.clipPlane[1] = g_pass.clipPlane[2] = g_pass.clipPlane[3] = 0.f;
	g_pass.gbufBound = savedGBuf;
	for (int k = 0; k < g_pmUsed; k++) HandOverViewTarget(ctx, g_pm[k]);
}

// [rc4l] In the player's view, before the translucent pass: each reflective piece painted from its plane's reflection.
void DrawPlaneMirrorSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_pass.mainView || g_pmUsed == 0 || !g_pmPSO || !g_scene.vb) return;
	Diligent::IBuffer *vbs[] = { g_scene.vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_pmPSO);
	int bound = -1;
	for (unsigned i = 0; i < g_scene.blendBatches.Size(); i++)
	{
		const SceneBatch &b = g_scene.batches[g_scene.blendBatches[i]];
		if (b.mirrorSlot < 0 || b.mirrorSlot >= g_pmUsed || b.mirrorSlot >= (int)g_pm.Size() || !g_pm[b.mirrorSlot].srb) continue;
		if (b.mirrorSlot != bound)
		{
			ctx->CommitShaderResources(g_pm[b.mirrorSlot].srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
			bound = b.mirrorSlot;
		}
		Diligent::DrawAttribs d;
		d.NumVertices = b.count;
		d.StartVertexLocation = b.first;
		ctx->Draw(d);
	}
}

// [rc4l] Stacked sectors as GL's GLSectorStackPortal draws them: each view the player's walk saw is the world from the eye moved by its portal, clipped at its planes, and the planes are painted from that picture.
// [rc4l] One target per stacked view the level can show (StackViewCapacity), all made before any comes into view.
static TArray<ViewTarget> g_stack;
static int g_stackDrawn = 0;   // [rc4l] Views rendered this frame.
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_stackShowSRB;   // [rc4l] fua_dg_stackshow's blit

void ReleaseStackPortals()
{
	g_stack.Clear();
	g_viewDepth.Release();
	g_stackShowSRB.Release();
	g_stackSurfPSO.Release();
	g_stackDrawn = 0;
	g_scene.stackVisReady = false;
}

// [rc4l] What GL's walks drew this frame and the player's sky holes, uploaded before any pass, outside the frame's render pass, since the stacked views and the main pass both read them.
void UploadWalkVisibility(Diligent::IDeviceContext *ctx)
{
	int visWords = 0;
	const unsigned int *visBits = zx::levelmesh::StackVisibility(visWords);
	g_scene.stackVisReady = visBits != NULL && g_scene.stackVisBuf && (unsigned int)visWords <= g_scene.stackVisCapacity;
	if (g_scene.stackVisReady)
		ctx->UpdateBuffer(g_scene.stackVisBuf, 0, (Diligent::Uint64)visWords * sizeof(unsigned int), visBits,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	int holes = 0;
	const float *hole = zx::levelmesh::SkyHoles(holes);
	g_scene.skyHoleCount = (g_scene.stackVisReady && hole != NULL && g_scene.skyHoleBuf) ? holes : 0;
	if (g_scene.skyHoleCount > 0)
		ctx->UpdateBuffer(g_scene.skyHoleBuf, 0, (Diligent::Uint64)g_scene.skyHoleCount * zx::levelmesh::kSkyHoleFloats * sizeof(float), hole,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

// [rc4l] Before the frame and after the sky's picture, which each view paints behind its own world.
void RenderStackPortals(Diligent::IDeviceContext *ctx)
{
	g_stackDrawn = 0;
	const int capacity = zx::levelmesh::StackViewCapacity();
	if (fua_dg_stackportals <= 0 || capacity <= 0 || !g_scene.vb || !g_cb) return;
	if (!g_stackSurfPSO)
		g_stackSurfPSO = CreateScreenSurfacePSO("fua stack surface", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
	if (!g_stackSurfPSO) return;
	if (!FitViewPool(g_stack, (unsigned)capacity, g_stackSurfPSO, "uSky", "fua stacked view")) return;
	const int n = zx::levelmesh::StackViewCount() < capacity ? zx::levelmesh::StackViewCount() : capacity;
	if (n <= 0) return;

	const fixed_t sx = viewx, sy = viewy;
	float savedMVP[16];
	for (int k = 0; k < 16; k++) savedMVP[k] = g_mvp[k];
	const bool savedGBuf = g_pass.gbufBound;
	g_pass.gbufBound = false;
	for (int k = 0; k < n; k++)
	{
		const zx::levelmesh::StackViewInfo v = zx::levelmesh::StackView(k);
		// [rc4l] No clip plane: GL's stacked walk clips nothing, and the cull already keeps to what it drew.
		viewx = sx + FLOAT2FIXED(v.dx);
		viewy = sy + FLOAT2FIXED(v.dy);
		g_pass.stackSlot = k;
		RenderWorldInto(ctx, g_stack[k]);
		g_stackDrawn = k + 1;
	}
	g_pass.stackSlot = -1;
	viewx = sx; viewy = sy;
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	g_pass.gbufBound = savedGBuf;
	for (int k = 0; k < g_stackDrawn; k++) HandOverViewTarget(ctx, g_stack[k]);
}

// [rc4l] What the stacked views did last frame, for fua_dg_stackinfo: views drawn, whether the cull had its bits, and how many subsectors each view's walk reached.
void StackInfo(FString &out)
{
	int words = 0;
	const unsigned int *bits = zx::levelmesh::StackVisibility(words);
	const int slots = zx::levelmesh::WalkSlots();
	TArray<int> reached;
	reached.Resize(slots);
	for (int s = 0; s < slots; s++) reached[s] = 0;
	for (int w = 0; w < words; w++)
		for (int b = 0; b < 32; b++)
			if (bits[w] & (1u << b)) reached[(w * 32 + b) % slots]++;
	const int view0 = zx::levelmesh::ComputeStackWalkSlot(0), view1 = zx::levelmesh::ComputeStackWalkSlot(1);
	out.Format("stacked views: %d drawn of %d the level can show, cull %s, %d words; subsectors and sidedefs drawn by view 0: %d, view 1: %d, "
		"the player's walk: %d, the skybox walk: %d (of %d); %d sky holes; plane mirrors: %d rendered of %d planes",
		g_stackDrawn, zx::levelmesh::StackViewCapacity(), g_scene.stackVisReady ? "on" : "off", words,
		view0 < slots ? reached[view0] : 0, view1 < slots ? reached[view1] : 0, reached[zx::levelmesh::kMainWalkSlot],
		reached[zx::levelmesh::kSkyboxWalkSlot], numsubsectors + numsides, g_scene.skyHoleCount, g_pmUsed, (int)g_pm.Size());
}

// [rc4l] In the player's view, with the sky: each stacked plane painted from its view's picture, and depth-written so nothing behind it shows through.
void DrawStackSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_pass.mainView || g_stackDrawn == 0 || !g_stackSurfPSO) return;
	static TArray<SceneVertex> sv;
	static TArray<unsigned int> first, count;
	first.Resize(g_stackDrawn);
	count.Resize(g_stackDrawn);
	sv.Clear();
	for (int k = 0; k < g_stackDrawn; k++)
	{
		int n = 0;
		const zx::levelmesh::MeshVertex *src = zx::levelmesh::StackViewVertices(k, n);
		first[k] = sv.Size();
		for (int i = 0; i < n; i++) sv.Push(ToSceneVertex(src[i], 0));
		// [rc4l] And the cap GL closes the stack's stencil with, so every unblocked ray past the plane sees the view, not only those through it.
		const bool ceiling = zx::levelmesh::StackView(k).ceiling;
		for (int c = 0; c < 6; c++)
		{
			float x, y, z;
			zx::levelmesh::ComputeStackCapCorner(ceiling, c, x, y, z);
			zx::levelmesh::MeshVertex fv;
			fv.Set(x, z, y, 0.f, 0.f);
			sv.Push(ToSceneVertex(fv, 0));
		}
		count[k] = sv.Size() - first[k];
	}
	if (!BindScreenSurfaces(ctx, g_stackSurfVB, g_stackSurfVBCapacity, sv, g_stackSurfPSO, "fua stack surface VB")) return;
	for (int k = 0; k < g_stackDrawn; k++)
		if (count[k] != 0 && g_stack[k].srb) DrawScreenSurfaceRun(ctx, g_stack[k].srb, first[k], count[k]);
}

// [rc4l] fua_dg_stackshow: the first stacked view's picture over the frame, through the sky portal's full-screen blit.
void ShowStackView(Diligent::IDeviceContext *ctx)
{
	if (!fua_dg_stackshow || g_stackDrawn == 0 || !SkyPortalPicturePSO() || !g_stack[0].color) return;
	if (!g_stackShowSRB) SkyPortalPicturePSO()->CreateShaderResourceBinding(&g_stackShowSRB, true);
	if (!g_stackShowSRB) return;
	if (auto *v = g_stackShowSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(g_stack[0].color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	ctx->SetPipelineState(SkyPortalPicturePSO());
	ctx->CommitShaderResources(g_stackShowSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = 3;
	ctx->Draw(dr);
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
	auto *rtv = pic->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	auto *dsv = pic->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	// Clip to the mirror's own plane, in MESH space: x and z there are the map's x and y.
	g_pass.clipPlane[0] = m.nx; g_pass.clipPlane[1] = 0.f; g_pass.clipPlane[2] = m.ny;
	g_pass.clipPlane[3] = -m.d;
	if (g_pass.clipPlane[3] == 0.f) g_pass.clipPlane[3] = 0.0001f;   // w == 0 means "off"
	ctx->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const int outer = g_mirrorPass;
	g_mirrorPass = node;
	g_pass.lineMirror = m.line;
	DrawWorld(ctx);
	ForgetViewSkyLooks();
	g_pass.lineMirror = -1;
	g_mirrorPass = outer;
	g_pass.clipPlane[0] = g_pass.clipPlane[1] = g_pass.clipPlane[2] = g_pass.clipPlane[3] = 0.f;
	ReadyMirrorPicture(ctx, pic->color);
}

void RenderMirrors(Diligent::IDeviceContext *ctx)
{
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
	if (!g_mirrorPSO || !g_mirrorSRB || !g_mirrorVB)
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
