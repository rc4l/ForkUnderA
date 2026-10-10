// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The horizon pass: each window writes its own depth and is painted by ray-casting the plane beyond -- see features/horizon.

#include "gl/system/gl_system.h"

#include "MapHelper.hpp"
#include "RefCntAutoPtr.hpp"

#include "features/hwrender/diligent/dgshared.h"
#include "features/hwrender/diligent/dglight_glsl.h"
#include "features/horizon/horizon.h"
#include "features/horizon/computation/horizon_compute.h"
#include "features/hwrender/computation/batch_compute.h"
#include "features/hwrender/computation/skyportal_compute.h"
#include "features/levelmesh/computation/packedcolor_compute.h"
#include "features/levelmesh/computation/wallbatch_compute.h"

#include "c_cvars.h"
#include "c_dispatch.h"   // [rc4l] CCMD, for fua_horizons
#include "tarray.h"

#include <algorithm>
#include <stdint.h>
#include <string.h>

namespace zx { namespace hwrender {

// [rc4l] Must equal kHorizonHalfExtent: a GLSL string cannot read the C++ constant.
#define FUA_HORIZON_EXTENT_STR "32768.0"
static_assert(zx::horizon::kHorizonHalfExtent == 32768.f, "update FUA_HORIZON_EXTENT_STR to match");

// [rc4l] One vertex carries its whole half-window, since a level has a handful of them.
struct HorizonVertex
{
	float x, y, z;                          // [rc4l] mesh space: (x, up, y)
	float r, g, b, softLight;               // [rc4l] CaptureShading's colour and soft light
	float fogR, fogG, fogB, fogMode;
	float planeZ, floorZ, ceilingZ, half;   // [rc4l] half: 0 the floor below the split, 1 the ceiling above
	float ux, uy, u0, fogDensity;
	float vx, vy, v0, pad;
};

static const char *kHorizonVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec4 aColorLight;\n"
	"layout(location = 2) in vec4 aFog;\n"
	"layout(location = 3) in vec4 aPlane;\n"
	"layout(location = 4) in vec4 aUVu;\n"
	"layout(location = 5) in vec4 aUVv;\n"
	FUA_CONSTANTS_GLSL
	"layout(location = 0) out vec3 vWorld;\n"
	"layout(location = 1) flat out vec3 vEye;\n"
	"layout(location = 2) flat out vec4 vColorLight;\n"
	"layout(location = 3) flat out vec4 vFog;\n"
	"layout(location = 4) flat out vec4 vPlane;\n"
	"layout(location = 5) flat out vec4 vUVu;\n"
	"layout(location = 6) flat out vec4 vUVv;\n"
	"void main() {\n"
	"    gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"    vWorld = aPos;\n"
	// [rc4l] The eye of whichever pass is drawing, read off its matrix, since uCameraPos holds only the player's.
	"    vec4 e = inverse(uMVP) * vec4(0.0, 0.0, 1.0, 0.0);\n"
	"    vEye = e.xyz / e.w;\n"
	"    vColorLight = aColorLight;\n"
	"    vFog = aFog;\n"
	"    vPlane = aPlane;\n"
	"    vUVu = aUVu;\n"
	"    vUVv = aUVv;\n"
	"}\n";

// [rc4l] The ray is ComputeHorizonRayHit, line for line; the light is the world's own.
static const char *kHorizonPS =
	"#version 450\n"
	FUA_SECTOR_LIGHT_GLSL
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(binding = 5) uniform sampler2D uBrightmap;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) flat in vec3 vEye;\n"
	"layout(location = 2) flat in vec4 vColorLight;\n"
	"layout(location = 3) flat in vec4 vFog;\n"
	"layout(location = 4) flat in vec4 vPlane;\n"
	"layout(location = 5) flat in vec4 vUVu;\n"
	"layout(location = 6) flat in vec4 vUVv;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    const float extent = " FUA_HORIZON_EXTENT_STR ";\n"
	"    vec3 d = vWorld - vEye;\n"
	"    float toPlane = vPlane.x - vEye.y;\n"
	"    vec2 o;\n"
	"    if (d.y * toPlane > 0.0) o = d.xz * (toPlane / d.y);\n"
	"    else { float len = length(d.xz); o = (len > 0.0) ? d.xz / len * (2.0 * extent) : vec2(0.0); }\n"
	"    float m = max(abs(o.x), abs(o.y));\n"
	"    if (m > extent) o *= extent / m;\n"
	"    vec3 hit = vec3(vEye.x + o.x, vPlane.x, vEye.z + o.y);\n"
	"    vec2 uv = vec2(dot(vUVu.xyz, vec3(hit.x, hit.z, 1.0)), dot(vUVv.xyz, vec3(hit.x, hit.z, 1.0)));\n"
	// [rc4l] Sampled BEFORE any discard, so the derivatives that pick the mip stay defined.
	"    vec3 texel = texture(uTex, uv).rgb;\n"
	"    vec3 bright = texture(uBrightmap, uv).rgb;\n"
	// [rc4l] ComputeHorizonSplit's rule, so a quad paints only its own half.
	"    float split = clamp(vEye.y, vPlane.y, vPlane.z);\n"
	"    bool ceilingHalf = vWorld.y >= split;\n"
	"    if (ceilingHalf != (vPlane.w > 0.5)) discard;\n"
	"    if (uClipPlane.w != 0.0 && dot(vWorld, uClipPlane.xyz) + uClipPlane.w < 0.0) discard;\n"
	"    if (uCameraPos.w == 0.0) { outColor = vec4(texel * vColorLight.rgb, 1.0); return; }\n"
	"    vec4 clip = uMVP * vec4(hit, 1.0);\n"
	"    float fogfactor = fuaFogFactor(vFog.a, vUVu.w, vec4(hit, clip.w), vEye);\n"
	"    vec3 color = fuaSectorLight(vColorLight.rgb, vColorLight.a, vFog.a, fogfactor, clip.z / clip.w);\n"
	"    color = min(color + bright, vec3(1.0));\n"
	"    outColor = vec4(fuaColouredFog(texel * color, vFog.rgb, vFog.a, fogfactor), 1.0);\n"
	"}\n";

static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_hzPSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_hzVB;
static unsigned int g_hzVBCapacity = 0;
static bool         g_hzFailed = false;
static int          g_hzBusiestHalves = 0, g_hzBusiestDraws = 0;   // [rc4l] the busiest pass since fua_horizons last asked

static bool EnsureHorizonPipeline()
{
	if (g_hzPSO) return true;
	if (g_hzFailed) return false;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua horizon VS", kHorizonVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua horizon PS", kHorizonPS);
	// [rc4l] Said once and then left alone, so a shader that will not compile costs one line, not a frame.
	if (!vs || !ps) { g_hzFailed = true; Printf("vulkan horizons OFF: shader compilation failed\n"); return false; }

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{4, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{5, 0, 4, Diligent::VT_FLOAT32, false},
	};
	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	// [rc4l] The world's own filter, and WRAP: a flat tiles out to the horizon.
	static Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
	};

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua horizon PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	// [rc4l] The side test is made per pass on the CPU, from that pass's own eye, so no winding has to agree.
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
	// [rc4l] Depth written, LESS_EQUAL: the window occludes what lies beyond the line, as a wall would.
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
	pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = (Diligent::Uint32)(sizeof(layout) / sizeof(layout[0]));
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = (Diligent::Uint32)(sizeof(vars) / sizeof(vars[0]));
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = (Diligent::Uint32)(sizeof(samplers) / sizeof(samplers[0]));
	pci.pVS = vs;
	pci.pPS = ps;
	dev->CreateGraphicsPipelineState(pci, &g_hzPSO);
	if (!g_hzPSO) { g_hzFailed = true; Printf("vulkan horizons OFF: pipeline creation failed\n"); return false; }
	for (int st = 0; st < 2; st++)
	{
		const Diligent::SHADER_TYPE stage = st ? Diligent::SHADER_TYPE_PIXEL : Diligent::SHADER_TYPE_VERTEX;
		if (auto *v = g_hzPSO->GetStaticVariableByName(stage, "Constants")) v->Set(SceneConstantsCB());
	}
	return true;
}

namespace {

struct HalfRef
{
	const zx::horizon::HorizonWindow *w;
	const zx::horizon::HorizonPlane *p;
	bool ceiling;
	float bottom, top;   // [rc4l] the slice this half paints from this pass's eye
};

void EmitHalf(const HalfRef &h, TArray<HorizonVertex> &out)
{
	const zx::horizon::HorizonWindow &w = *h.w;
	const zx::horizon::HorizonPlane &p = *h.p;
	HorizonVertex v;
	memset(&v, 0, sizeof(v));
	v.r = p.shade.colorR; v.g = p.shade.colorG; v.b = p.shade.colorB;
	v.softLight = p.shade.softLight;
	zx::levelmesh::ComputeUnpackRGB(p.shade.fogColor, v.fogR, v.fogG, v.fogB);
	v.fogMode = (float)p.shade.fogMode;
	v.planeZ = p.z; v.floorZ = w.floorZ; v.ceilingZ = w.ceilingZ;
	v.half = h.ceiling ? 1.f : 0.f;
	v.ux = p.uv.ux; v.uy = p.uv.uy; v.u0 = p.uv.u0; v.fogDensity = p.shade.fogDensity;
	v.vx = p.uv.vx; v.vy = p.uv.vy; v.v0 = p.uv.v0;

	const float xs[4] = { w.x1, w.x1, w.x2, w.x2 };
	const float ys[4] = { w.y1, w.y1, w.y2, w.y2 };
	const float zs[4] = { h.bottom, h.top, h.top, h.bottom };
	for (int t = 0; t < 2; t++)
		for (int k = 0; k < 3; k++)
		{
			const int c = zx::levelmesh::ComputeFanTriangleVertex(4, t, k);
			v.x = xs[c]; v.y = zs[c]; v.z = ys[c];
			out.Push(v);
		}
}

} // namespace

void DrawHorizonPlanes(Diligent::IDeviceContext *ctx)
{
	int n = 0;
	const zx::horizon::HorizonWindow *windows = zx::horizon::HorizonWindows(n);
	if (windows == NULL || n <= 0 || ctx == NULL) return;
	if (!EnsureHorizonPipeline()) return;

	float camX = 0.f, camY = 0.f, reach = 0.f;
	const bool inPortal = SkyPortalReach(camX, camY, reach);
	// [rc4l] This pass's own eye, read off the matrix it draws with -- the shader reads the same one.
	const float *mvp = SceneMVP();
	float eye[3];
	if (!zx::horizon::ComputeEyeFromViewProjection(mvp, eye)) return;

	static TArray<HalfRef> halves;
	halves.Clear();
	for (int i = 0; i < n; i++)
	{
		const zx::horizon::HorizonWindow &w = windows[i];
		if (w.ceilingZ <= w.floorZ) continue;
		// [rc4l] Inside the sky portal, only what stands near its camera -- the same reach the world takes.
		if (inPortal && ComputeBeyondSkyPortalReach(w.x1 < w.x2 ? w.x1 : w.x2, w.x1 > w.x2 ? w.x1 : w.x2,
		                                            w.y1 < w.y2 ? w.y1 : w.y2, w.y1 > w.y2 ? w.y1 : w.y2,
		                                            camX, camY, reach))
			continue;
		// [rc4l] Mesh space is (x, up, y), so the eye's map position is its x and z.
		if (!zx::horizon::ComputeHorizonFacesEye(w.x1, w.y1, w.x2, w.y2, eye[0], eye[2])) continue;
		for (int half = 0; half < 2; half++)
		{
			const zx::horizon::HorizonPlane &p = half ? w.ceiling : w.floor;
			if (!p.valid) continue;
			const zx::horizon::HorizonSpan s =
				zx::horizon::ComputeHorizonHalfSpan(half != 0, w.floorZ, w.ceilingZ, eye[1]);
			if (s.top - s.bottom < 0.01f) continue;
			const float corners[4][3] = {
				{ w.x1, s.bottom, w.y1 }, { w.x1, s.top, w.y1 }, { w.x2, s.top, w.y2 }, { w.x2, s.bottom, w.y2 } };
			if (!zx::horizon::ComputeQuadMayBeVisible(mvp, corners)) continue;
			HalfRef h = { &w, &p, half != 0, s.bottom, s.top };
			halves.Push(h);
		}
	}
	if (halves.Size() == 0) return;
	// [rc4l] Grouped by material, so each texture is one binding and one draw.
	std::sort(&halves[0], &halves[0] + halves.Size(),
		[](const HalfRef &a, const HalfRef &b) { return a.p->material < b.p->material; });

	static TArray<HorizonVertex> verts;
	verts.Clear();
	for (unsigned i = 0; i < halves.Size(); i++) EmitHalf(halves[i], verts);

	if (!g_hzVB || g_hzVBCapacity < verts.Size())
	{
		g_hzVBCapacity = verts.Size() * 2 + 64;
		Diligent::BufferDesc bd;
		bd.Name = "fua horizon VB";
		bd.Size = (Diligent::Uint64)g_hzVBCapacity * sizeof(HorizonVertex);
		// [rc4l] DYNAMIC, so filling it is a map rather than a copy -- this runs inside the frame's pass.
		bd.Usage = Diligent::USAGE_DYNAMIC;
		bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		g_hzVB.Release();
		GetDevice()->CreateBuffer(bd, nullptr, &g_hzVB);
		if (!g_hzVB) { g_hzVBCapacity = 0; return; }
	}
	{
		Diligent::MapHelper<HorizonVertex> m(ctx, g_hzVB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		memcpy((HorizonVertex *)m, &verts[0], verts.Size() * sizeof(HorizonVertex));
	}

	Diligent::IBuffer *vbs[] = { g_hzVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_hzPSO);

	static TArray<uint64_t> keys;
	static TArray<::hwrender::BatchRun> runs;
	keys.Resize(halves.Size());
	runs.Resize(halves.Size());
	for (unsigned i = 0; i < halves.Size(); i++) keys[i] = (uint64_t)(uintptr_t)halves[i].p->material;
	const int nruns = ::hwrender::ComputeBatchRuns(&keys[0], (int)keys.Size(), &runs[0], (int)runs.Size());
	if ((int)halves.Size() > g_hzBusiestHalves) { g_hzBusiestHalves = (int)halves.Size(); g_hzBusiestDraws = nruns; }
	for (int r = 0; r < nruns; r++)
	{
		const ::hwrender::BatchRun &run = runs[r];
		Diligent::IShaderResourceBinding *srb = GetMaterialSRB(g_hzPSO, halves[run.start].p->material, 0);
		if (srb == NULL) continue;
		ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::DrawAttribs dr;
		dr.NumVertices = (Diligent::Uint32)run.count * 6;
		dr.StartVertexLocation = (Diligent::Uint32)run.start * 6;
		dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(dr);
	}
}

}} // namespace zx::hwrender

// [rc4l] The windows, and the most any one pass has drawn of them since this was last asked.
CCMD(fua_horizons)
{
	int n = 0;
	const zx::horizon::HorizonWindow *w = zx::horizon::HorizonWindows(n);
	int sky = 0;
	zx::horizon::HorizonSkyVertices(sky);
	Printf("horizons: %d windows, %d sky quads; the busiest pass drew %d halves in %d draws\n",
		n, sky / 6, zx::hwrender::g_hzBusiestHalves, zx::hwrender::g_hzBusiestDraws);
	zx::hwrender::g_hzBusiestHalves = zx::hwrender::g_hzBusiestDraws = 0;
	for (int i = 0; i < n && i < 16; i++)
		Printf("  (%.0f,%.0f)-(%.0f,%.0f) floor %.0f ceiling %.0f  floor plane %s  ceiling %s\n",
			w[i].x1, w[i].y1, w[i].x2, w[i].y2, w[i].floorZ, w[i].ceilingZ,
			w[i].floor.valid ? "yes" : "no", w[i].skyAbove ? "sky" : (w[i].ceiling.valid ? "plane" : "none"));
}
