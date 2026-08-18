// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The decal pass: one box per mark, resolved per fragment.
//
// Split out of dgscene.cpp, which had grown to four thousand lines and was carrying the whole of
// this alongside the world, the sky, the sprites and the blended pass. Nothing here touches the
// scene's state -- it reads the depth and normal the world wrote, and the view matrix it wrote them
// with, through dgshared.h -- so it has no business living in the same file.
//
// What the marks themselves are, and why they are boxes, is in features/levelmesh/projdecals.h.

#include "gl/system/gl_system.h"

#include "MapHelper.hpp"
#include "RefCntAutoPtr.hpp"

#include "features/hwrender/diligent/dgshared.h"
#include "features/levelmesh/projdecals.h"

#include "c_cvars.h"
#include "tarray.h"
#include "templates.h"

#include <math.h>

EXTERN_CVAR(Int, fua_decalmode)

// [rc4l] ANGLE FADE: how much a surface met at a slant loses.
//
// Soot lands in proportion to the cosine of the angle it arrives at, the same reason a lamp at a
// grazing angle lights a wall less. This is per FRAGMENT, read from the same normal buffer the
// surface itself wrote, so it varies continuously across a corner and there is no seam to make --
// which is exactly what the CPU-clipped version of this could not do, because there the fade was
// constant across each surface and a corner took two different constants.
//
// 0 leaves every surface at full strength; 1 is the plain cosine.
CVAR(Float, fua_decal_anglefade, 0.65f, CVAR_ARCHIVE)

namespace zx { namespace hwrender {

// ---------------------------------------------------------------------------
// Deferred decals, with one texture array
// ---------------------------------------------------------------------------
//
// [rc4l] A mark is drawn as its own BOX, and resolved per fragment against the depth and normal the
// world has already written. Nothing is cut, nothing is glued, and the mark is never made of pieces.
//
// That last point is the whole reason for this pass. The mesh path cuts the geometry inside the box
// into triangles on the CPU, and a triangle is the smallest thing that can carry an alpha -- so a
// mark that has to fade as it reaches away from the impact fades in SLICES, and every seam between
// two surfaces is a place where two constants meet. Here the fade is a length in the pixel stage,
// which is continuous everywhere including across a corner, and costs nothing to make smooth.
//
// One array of textures, indexed per instance, so every mark in the level is one draw call whatever
// graphic it uses -- rather than one draw per texture, which is what a scorch and its glow
// alternating turned into seventy marks and fifty-five draws. That is the bindless part: the index
// travels in the instance record and the sampler array is bound once.
// [rc4l] How many different decal graphics one draw can reach.
//
// The article's version is unbounded -- a runtime-sized array with descriptor indexing -- and this is
// the same idea with a ceiling on it, because a fixed array needs only ShaderResourceStaticArrays
// where an unbounded one needs the full bindless path, and a Doom level uses tens of decal graphics
// rather than thousands. If a level ever exceeds this the marks past it fall back to the first slot
// rather than vanishing, and fua_dg_decalstats says it happened.
#define FUA_DECAL_TEXTURES     64
#define FUA_DECAL_TEXTURES_STR "64"

static const char *kDeferredDecalVS =
	"#version 450\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	/* One record per mark: where it landed, which way its picture is turned, how far it reaches,
	   what to paint, and which texture to paint it with. */
	"struct DecalRec { vec4 centre; vec4 axisU; vec4 axisV; vec4 axisN; vec4 color; vec4 params; };\n"
	"layout(std430, binding = 9) readonly buffer Decals { DecalRec decals[]; };\n"
	"layout(location = 0) out vec3 vCentre;\n"
	"layout(location = 1) out vec3 vAxisU;\n"
	"layout(location = 2) out vec3 vAxisV;\n"
	"layout(location = 3) out vec3 vAxisN;\n"
	"layout(location = 4) out vec4 vColor;\n"
	"layout(location = 5) out vec4 vParams;\n"
	"layout(location = 6) flat out int vTex;\n"
	"const vec3 kCorner[8] = vec3[8](\n"
	"    vec3(-1,-1,-1), vec3( 1,-1,-1), vec3( 1, 1,-1), vec3(-1, 1,-1),\n"
	"    vec3(-1,-1, 1), vec3( 1,-1, 1), vec3( 1, 1, 1), vec3(-1, 1, 1));\n"
	"const vec2 kQuad[6] = vec2[6](\n"
	"    vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1));\n"
	"void main() {\n"
	"    DecalRec d = decals[gl_InstanceIndex];\n"
	"    vCentre = d.centre.xyz;\n"
	"    vAxisU = d.axisU.xyz; vAxisV = d.axisV.xyz; vAxisN = d.axisN.xyz;\n"
	"    vColor = d.color;\n"
	"    vParams = d.params;\n"
	"    vTex = int(d.centre.w);\n"
	/* Where this mark's box lands on screen. Its eight corners are projected and the extremes kept
	   -- conservative, which is all a bound has to be, and it means the pixel stage only runs where
	   the mark could possibly be rather than over the whole screen for every mark. */
	"    float reach = max(d.params.x, max(d.params.y, d.params.z));\n"
	"    vec2 lo = vec2( 1e9), hi = vec2(-1e9);\n"
	"    bool whole = false;\n"
	/* Inside the box, or straddling the plane through the camera, and there are no honest screen
	   bounds to compute -- so take the whole screen and let the pixel stage decide. This is the case
	   that made a box vanish when you stood in it. */
	"    if (distance(uCameraPos.xyz, d.centre.xyz) < reach * 1.75) whole = true;\n"
	"    for (int i = 0; i < 8 && !whole; i++) {\n"
	"        vec4 cp = uMVP * vec4(d.centre.xyz + kCorner[i] * reach, 1.0);\n"
	"        if (cp.w <= 0.0001) { whole = true; break; }\n"
	"        vec2 ndc = cp.xy / cp.w;\n"
	"        lo = min(lo, ndc); hi = max(hi, ndc);\n"
	"    }\n"
	"    if (whole) { lo = vec2(-1.0); hi = vec2(1.0); }\n"
	"    lo = max(lo, vec2(-1.0)); hi = min(hi, vec2(1.0));\n"
	/* Straight to clip space. Depth is unused -- the pass neither tests nor writes it -- so the quad
	   sits at the near plane and nothing can clip it away. */
	"    vec2 q = kQuad[gl_VertexIndex];\n"
	"    gl_Position = vec4(mix(lo, hi, q), 0.0, 1.0);\n"
	"}\n";

static const char *kDeferredDecalPS =
	"#version 450\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	"layout(binding = 6) uniform Decal { mat4 uInvMVP; vec4 uDecalDebug; };\n"
	"layout(binding = 7) uniform sampler2D uSceneDepth;\n"
	"layout(binding = 8) uniform sampler2D uSceneNormal;\n"
	/* [rc4l] ONE texture, and the pass groups its marks by it.
	   An array indexed per instance -- the bindless form -- would make the whole level a single
	   draw, and the array binds and reflects its 64 slots without complaint here but every
	   element samples white, so it is parked rather than shipped half-working. Grouping costs a
	   draw per graphic, which on a Doom level is a handful. */
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) in vec3 vCentre;\n"
	"layout(location = 1) in vec3 vAxisU;\n"
	"layout(location = 2) in vec3 vAxisV;\n"
	"layout(location = 3) in vec3 vAxisN;\n"
	"layout(location = 4) in vec4 vColor;\n"
	"layout(location = 5) in vec4 vParams;\n"
	"layout(location = 6) flat in int vTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / vec2(uScreen.x, uScreen.y);\n"
	"    float d = texture(uSceneDepth, uv).r;\n"
	/* Nothing was drawn here -- the far plane. There is no surface to paint. */
	"    if (d >= 1.0) discard;\n"
	/* Reconstruct the world position of whatever the depth buffer says is here. Clip space is x,y in
	   -1..1 and z in 0..1, the Vulkan convention this backend's projection uses.
	   [rc4l] Y is flipped between the framebuffer and clip space: gl_FragCoord.y counts DOWN from the
	   top while clip-space y runs up, and reconstructing without the flip mirrors the world position
	   vertically. Because the error grows with distance from the centre of the screen, the mark then
	   appears to slide about as the camera turns -- "it follows the camera" is what that looks like. */
	"    vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);\n"
	"    vec4 world = uInvMVP * clip;\n"
	"    vec3 P = world.xyz / world.w;\n"
	"    vec3 rel = P - vCentre;\n"
	/* Into the picture's own axes. U and V arrive divided by their half-extents, so the picture is
	   whatever falls in -1..1; N is unit, so the depth test is in world units and asymmetric -- the
	   box reaches further back towards the shooter than forward through the surface. */
	"    float u = dot(rel, vAxisU);\n"
	"    float v = dot(rel, vAxisV);\n"
	"    float w = dot(rel, vAxisN);\n"
	"    if (abs(u) > 1.0 || abs(v) > 1.0) discard;\n"
	"    if (w < -vParams.x || w > vParams.y) discard;\n"
	/* [rc4l] The surface's EXACT normal, out of the G-buffer. Estimating it from four depth taps was
	   wrong in every place that mattered: across a silhouette it straddled two surfaces, on a
	   two-pixel sliver there was no neighbourhood to sample, and at a grazing angle it was a
	   difference of nearly-equal large numbers. Alpha zero means nothing was drawn here. */
	"    vec4 gbuf = texture(uSceneNormal, uv);\n"
	"    if (gbuf.a < 0.5) discard;\n"
	"    vec3 nrm = normalize(gbuf.xyz * 2.0 - 1.0);\n"
	/* A surface facing away from the projectile cannot have been sprayed by it. Without this a pillar
	   prints a second, mirrored copy of the mark on the face nobody shot at. */
	/* A surface facing away from the projectile cannot have been sprayed by it. Without this a
	   pillar prints a second, mirrored copy of the mark on the face nobody shot at. */
	"    float facing = -dot(nrm, vAxisN);\n"
	"    if (facing < 0.05) discard;\n"
	"    vec2 t = vec2(u * 0.5 + 0.5, 0.5 - v * 0.5);\n"
	/* [rc4l] uDecalDebug.z pins every mark to slot zero, which is the one experiment that tells
	   a bad INDEX from a bad BINDING apart: if the mark appears when pinned, the array is bound
	   and the index is wrong; if it stays blank, nothing is bound and the index is innocent. */
	"    vec4 texel = texture(uTex, t);\n"
	/* A shaded decal's texture is an alpha MASK: the red channel is the shape and the colour comes
	   from the decal itself. Sampled as an ordinary image the red channel reads as brightness and a
	   black burn paints a white blob. */
	"    bool redAlpha = vParams.w > 0.5;\n"
	"    float mask = redAlpha ? texel.r : texel.a;\n"
	"    float a = mask * vColor.a;\n"
	/* [rc4l] ANGLE FADE: how squarely the blast met this surface.
	   Soot lands in proportion to the cosine of the angle it arrives at, the same reason a lamp at a
	   grazing angle lights a wall less. The mesh path has this too and has it turned OFF, because
	   there the fade is constant across each surface -- so at a corner the two faces take different
	   constants and a hard step appears down the join. Here it is a per-FRAGMENT quantity read from
	   the same normal buffer the surface itself wrote, so it varies continuously across a corner and
	   there is no seam to make. This is the version of the idea that actually works.
	   uDecalDebug.y is the strength: 0 leaves every surface at full, 1 is the plain cosine. */
	"    a *= mix(1.0, facing, clamp(uDecalDebug.y, 0.0, 1.0));\n"
	/* [rc4l] The run-out, per fragment. Inside the picture's own radius nothing fades, so the mark on
	   the surface that was actually hit is exactly what the decal says it is; beyond that the mark is
	   reaching onto geometry the picture never covered and it runs out smoothly.
	   Radial, so it does not know a corner is there -- which is why there is no step at one. This is
	   the same rule the mesh path applies in slices, done properly. */
	/* The picture's own corner radius, recovered from the axes: they arrive divided by their
	   half-extents, so the length of one is the reciprocal of the other. Cheaper than another
	   slot in the record, and it cannot fall out of step with the axes it is derived from. */
	"    float r = length(rel);\n"
	"    float hw = 1.0 / max(length(vAxisU), 1e-6);\n"
	"    float hh = 1.0 / max(length(vAxisV), 1e-6);\n"
	"    float inner = sqrt(hw*hw + hh*hh);\n"
	"    if (vParams.z > inner) a *= 1.0 - smoothstep(inner, vParams.z, r);\n"
	"    if (a <= 0.004) discard;\n"
	/* 1 shows the picture coordinate, 2 the mask that came out of the texture, 3 how squarely the
	   surface was met. A mark that is wrong looks the same whichever of the three is at fault. */
	"    if (uDecalDebug.x > 0.5) {\n"
	"        if (uDecalDebug.x < 1.5) outColor = vec4(fract(t), 0.0, 1.0);\n"
	"        else if (uDecalDebug.x < 2.5) outColor = vec4(vec3(mask), 1.0);\n"
	"        else outColor = vec4(vec3(facing), 1.0);\n"
	"        return;\n"
	"    }\n"
	/* [rc4l] An alpha-mask decal takes its colour from the DECAL, not from the texture.
	   Multiplying the two together looks harmless and is not: the mask lives in the red channel,
	   so a green glow times (mask, 0, 0) is black, and the BFG's glow would disappear into its
	   own scorch. An ordinary decal is the other way round -- the texture is the picture and the
	   colour is white. */
	"    vec3 rgb = redAlpha ? vColor.rgb : (vColor.rgb * texel.rgb);\n"
	"    outColor = vec4(rgb, a);\n"
	"}\n";

// [rc4l] Show what the pass is computing instead of what it paints: the picture coordinate, so a
// mark that is being cut somewhere says WHERE rather than merely being absent. Every term that can
// discard a fragment produces the same result on screen, and guessing which one cost several rounds.
CVAR(Int, fua_dg_decaldebug, 0, 0)

// [rc4l] Pin every mark to the first texture slot. See the pixel shader: it separates a wrong
// index from an unbound array, which look identical on screen.
CVAR(Bool, fua_dg_decalslot0, false, 0)

// [rc4l] One record per mark, read by the vertex stage -- see kDeferredDecalVS.
struct DeferredDecalInstance
{
	float centre[4];   // xyz where it landed (mesh space), w = which texture slot
	float axisU[4];    // xyz the picture's across-axis, divided by its half-width
	float axisV[4];    // xyz the picture's up-axis, divided by its half-height
	float axisN[4];    // xyz unit, the way the projectile was going
	float color[4];    // rgb tint and alpha, already faded
	float params[4];   // near, far, run-out radius, red-as-alpha flag
};

static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_ddPSO;      // ordinary blend
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_ddAddPSO;   // additive
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_ddSRB;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_ddAddSRB;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_ddInstBuf;
static Diligent::IBufferView *g_ddInstSRV = NULL;
static unsigned int g_ddCapacity = 0;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_ddCB;
static int g_ddDrawn = 0, g_ddBoxes = 0, g_ddTextures = 0;
static const char *g_ddBail = "";

void GetDeferredDecalStats(int &boxes, int &draws, int &textures, const char **bail)
{
	boxes = g_ddBoxes; draws = g_ddDrawn; textures = g_ddTextures;
	if (bail) *bail = g_ddBail;
}

void ReleaseDeferredDecalPass()
{
	g_ddSRB.Release();
	g_ddAddSRB.Release();
	g_ddPSO.Release();
	g_ddAddPSO.Release();
}

static bool EnsureDeferredDecalPass()
{
	if (g_ddPSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::ShaderCreateInfo ci;
	ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
	ci.Desc.Name = "fua deferred decal VS";
	ci.Source = kDeferredDecalVS;
	dev->CreateShader(ci, &vs);
	ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
	ci.Desc.Name = "fua deferred decal PS";
	ci.Source = kDeferredDecalPS;
	dev->CreateShader(ci, &ps);
	if (!vs || !ps) { g_ddBail = "shader compilation failed"; return false; }

	if (!g_ddInstBuf)
	{
		g_ddCapacity = 1024;
		Diligent::BufferDesc bd;
		bd.Name = "fua deferred decal instances";
		bd.Size = (Diligent::Uint64)g_ddCapacity * sizeof(DeferredDecalInstance);
		bd.Usage = Diligent::USAGE_DEFAULT;
		bd.BindFlags = Diligent::BIND_SHADER_RESOURCE;
		bd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
		bd.ElementByteStride = sizeof(DeferredDecalInstance);
		dev->CreateBuffer(bd, nullptr, &g_ddInstBuf);
		if (!g_ddInstBuf) { g_ddCapacity = 0; g_ddBail = "instance buffer creation failed"; return false; }
		g_ddInstSRV = g_ddInstBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE);
	}

	if (!g_ddCB)
	{
		Diligent::BufferDesc bd;
		bd.Name = "fua deferred decal constants";
		bd.Size = 20 * sizeof(float);   // the inverse view-projection, plus a debug selector
		bd.Usage = Diligent::USAGE_DYNAMIC;
		bd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		bd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		dev->CreateBuffer(bd, nullptr, &g_ddCB);
		if (!g_ddCB) { g_ddBail = "constant buffer creation failed"; return false; }
	}

	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneDepth", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneNormal", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	// CLAMP both ways: the picture is bounded by the box, and a wrap would tile the graphic across
	// whatever the box happens to reach.
	samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	static Diligent::SamplerDesc depthSamp;
	depthSamp.MinFilter = depthSamp.MagFilter = depthSamp.MipFilter = Diligent::FILTER_TYPE_POINT;
	depthSamp.AddressU = depthSamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneDepth", depthSamp },
		{ Diligent::SHADER_TYPE_PIXEL, "uSceneNormal", depthSamp },
	};

	for (int pass = 0; pass < 2; pass++)
	{
		const bool additive = (pass == 1);
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = additive ? "fua deferred decal PSO additive" : "fua deferred decal PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
		// [rc4l] NO depth attachment, which is not the same as "depth test off".
		//
		// This pass READS the scene depth as a texture, and it is the texture the world pass just drew
		// into. Leaving it attached while sampling it is a read-write hazard on one resource: the
		// driver is entitled to serve stale or partially-resolved tiles, and it does -- a fine diagonal
		// hatch across every surface a decal reached, which reads exactly like z-fighting and is not.
		pci.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		// Nothing to cull and no depth to test: the geometry is a screen-space quad over the mark's
		// own bounds, so it has no inside to be caught in and each pixel is visited once from
		// anywhere. The shader finds the surface in the depth BUFFER; the quad's own depth is nothing.
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		{
			auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
			rt.BlendEnable = true;
			rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
			rt.DestBlend = additive ? Diligent::BLEND_FACTOR_ONE : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
			rt.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
			rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
		}
		// No input layout: the quad comes from gl_VertexIndex and the mark from gl_InstanceIndex.
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 3;
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 3;
		pci.pVS = vs;
		pci.pPS = ps;

		Diligent::RefCntAutoPtr<Diligent::IPipelineState> made;
		dev->CreateGraphicsPipelineState(pci, &made);
		if (!made) { g_ddBail = "pipeline creation failed"; return false; }
		for (int st = 0; st < 2; st++)
		{
			const Diligent::SHADER_TYPE stage = st ? Diligent::SHADER_TYPE_PIXEL : Diligent::SHADER_TYPE_VERTEX;
			if (auto *v = made->GetStaticVariableByName(stage, "Constants")) v->Set(SceneConstantsCB());
			if (auto *v = made->GetStaticVariableByName(stage, "Decal")) v->Set(g_ddCB);
			if (auto *v = made->GetStaticVariableByName(stage, "Decals")) v->Set(g_ddInstSRV);
		}
		if (additive) g_ddAddPSO = made; else g_ddPSO = made;
	}

	g_ddPSO->CreateShaderResourceBinding(&g_ddSRB, true);
	g_ddAddPSO->CreateShaderResourceBinding(&g_ddAddSRB, true);
	if (!g_ddSRB || !g_ddAddSRB) { g_ddBail = "resource binding creation failed"; return false; }

	return true;
}

// [rc4l] Draw this frame's marks as boxes, resolved against the depth and normal buffers.
//
// Runs AFTER the world and before the sprites: a decal marks a surface, so it belongs with the
// surfaces, and anything standing in front of one must be drawn over it. That is the whole of the
// ordering problem, settled by which pass a thing is in rather than by sorting a mark against every
// sprite in the room.
void DrawDeferredDecals(Diligent::IDeviceContext *ctx)
{
	g_ddDrawn = 0;
	g_ddBoxes = 0;
	g_ddTextures = 0;
	g_ddBail = "";

	const zx::levelmesh::GpuDecal *decals = NULL;
	const int n = zx::levelmesh::GetProjectedDecalsGpu(&decals);
	if (n <= 0 || decals == NULL) { g_ddBail = "nothing registered"; return; }
	if (!EnsureDeferredDecalPass()) return;

	Diligent::ITextureView *depthSRV = SceneDepthSRV();
	Diligent::ITextureView *normalSRV = SceneNormalSRV();
	if (!depthSRV) { g_ddBail = "no scene depth"; return; }
	if (!normalSRV) { g_ddBail = "no scene normal"; return; }

	float invMVP[16];
	if (!InvertMatrix4(SceneMVP(), invMVP)) { g_ddBail = "view matrix not invertible"; return; }
	{
		Diligent::MapHelper<float> cb(ctx, g_ddCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int i = 0; i < 16; i++) cb[i] = invMVP[i];
		cb[16] = (float)fua_dg_decaldebug;
		cb[17] = (float)fua_decal_anglefade;
		cb[18] = 0.f;
		cb[19] = 0.f;
	}

	// [rc4l] Every graphic this frame needs, gathered into ONE array the shader indexes per mark.
	//
	// This is what makes the whole level one draw. Grouping by texture instead -- which is what a
	// single bound sampler forces -- costs a draw call every time the graphic changes, and a scorch
	// and its glow alternate, so seventy marks came to fifty-five draws. The slot travels in the
	// instance record and nothing has to be sorted to keep it.
	static TArray<const void *> mats;
	static TArray<Diligent::IDeviceObject *> views;
	static TArray<DeferredDecalInstance> instAlpha, instAdd;
	static TArray<const void *> matAlpha, matAdd, matOf;
	mats.Clear(); views.Clear(); instAlpha.Clear(); instAdd.Clear();
	matAlpha.Clear(); matAdd.Clear(); matOf.Clear();

	Diligent::IDeviceObject *fallback = GetMaterialSRV(NULL, 0);
	for (int i = 0; i < n; i++)
	{
		const zx::levelmesh::GpuDecal &d = decals[i];
		if (d.a <= 0.004f || d.material == NULL) continue;

		int slot = -1;
		for (unsigned m = 0; m < mats.Size(); m++) if (mats[m] == d.material) { slot = (int)m; break; }
		if (slot < 0)
		{
			if (mats.Size() >= FUA_DECAL_TEXTURES) slot = 0;   // over the ceiling: see FUA_DECAL_TEXTURES
			else
			{
				Diligent::IDeviceObject *srv = GetMaterialSRV(d.material, 0);
				if (!srv) continue;
				// [rc4l] Say when a mark's texture came back as the WHITE fallback.
				//
				// GetMaterialSRV answers white for anything it cannot produce, which is the right default
				// and an invisible failure: a mask of 1 everywhere paints the mark's whole box solid, and
				// that looks like a clipping fault, a blend fault or a UV fault in turn.
				if (fua_dg_decaldebug > 0 && srv == GetMaterialSRV(NULL, 0))
					Printf("decal texture fell back to WHITE for material %p\n", d.material);
				slot = (int)mats.Size();
				mats.Push(d.material);
				views.Push(srv);
			}
		}

		DeferredDecalInstance inst;
		inst.centre[0] = d.centre[0]; inst.centre[1] = d.centre[1]; inst.centre[2] = d.centre[2];
		inst.centre[3] = (float)slot;
		inst.axisU[0] = d.right[0]; inst.axisU[1] = d.right[1]; inst.axisU[2] = d.right[2]; inst.axisU[3] = 0.f;
		inst.axisV[0] = d.up[0];    inst.axisV[1] = d.up[1];    inst.axisV[2] = d.up[2];    inst.axisV[3] = 0.f;
		inst.axisN[0] = d.axis[0];  inst.axisN[1] = d.axis[1];  inst.axisN[2] = d.axis[2];  inst.axisN[3] = 0.f;
		inst.color[0] = d.r; inst.color[1] = d.g; inst.color[2] = d.b; inst.color[3] = d.a;
		inst.params[0] = d.near_;
		inst.params[1] = d.far_;
		// Where the run-out ends: the picture's own corner radius plus the box's reach behind it.
		// Inside the corner radius nothing fades, which is stated in the shader.
		inst.params[2] = sqrtf(d.halfW*d.halfW + d.halfH*d.halfH) + d.near_;
		inst.params[3] = d.redToAlpha ? 1.f : 0.f;
		if (d.additive) { instAdd.Push(inst); matAdd.Push(d.material); }
		else { instAlpha.Push(inst); matAlpha.Push(d.material); }
	}

	const unsigned total = instAlpha.Size() + instAdd.Size();
	if (total == 0) { g_ddBail = "every mark filtered out (no texture, or faded to nothing)"; return; }
	g_ddBoxes = (int)total;
	g_ddTextures = (int)mats.Size();

	// Pad the array: an unbound slot in a sampler array reads as undefined, and the shader may index
	// any of them from any instance in the draw.
	while (views.Size() < FUA_DECAL_TEXTURES) views.Push(fallback);

	// [rc4l] Ordinary blend first, additive second. Additive only ever brightens, so nothing can
	// meaningfully be drawn over it and it can very easily be drawn under something and lost -- a
	// scorch and the glow that belongs on top of it land in exactly the same place.
	static TArray<DeferredDecalInstance> all;
	all.Clear();
	for (unsigned i = 0; i < instAlpha.Size(); i++) { all.Push(instAlpha[i]); matOf.Push(matAlpha[i]); }
	for (unsigned i = 0; i < instAdd.Size(); i++) { all.Push(instAdd[i]); matOf.Push(matAdd[i]); }
	if (all.Size() > g_ddCapacity) all.Resize(g_ddCapacity);
	ctx->UpdateBuffer(g_ddInstBuf, 0, (Diligent::Uint64)all.Size() * sizeof(DeferredDecalInstance),
		&all[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// The depth buffer comes off the render target for this pass -- it is being READ.
	auto *swap = GetSwapChain();
	Diligent::ITextureView *rtv = swap ? swap->GetCurrentBackBufferRTV() : NULL;
	if (rtv) ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// [rc4l] Grouped by texture and blend, which are the only two things that force a new draw.
	//
	// Everything else about a mark travels in its own instance record, so a hundred marks sharing a
	// scorch graphic are one draw of a hundred instances. The groups keep ARRIVAL order rather than
	// being sorted by material pointer: order is the picture where two marks overlap, and a decal
	// template creates its LOWER decal first, so arrival order already says scorch and then glow.
	// Sorting on the pointer instead decides that by wherever the allocator put two textures, which
	// is arbitrary and stable enough to look deliberate.
	const unsigned alphaCount = (instAlpha.Size() > all.Size()) ? all.Size() : instAlpha.Size();
	unsigned first = 0;
	while (first < all.Size())
	{
		const bool additive = (first >= alphaCount);
		const void *mat = matOf[first];
		unsigned last = first + 1;
		while (last < all.Size() && matOf[last] == mat && ((last >= alphaCount) == additive)) last++;

		Diligent::IPipelineState *pso = additive ? g_ddAddPSO.RawPtr() : g_ddPSO.RawPtr();
		if (pso)
		{
			auto *srb = GetMaterialSRB(pso, mat, 0);
			if (srb)
			{
				if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSceneDepth")) v->Set(depthSRV);
				if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSceneNormal")) v->Set(normalSRV);
				ctx->SetPipelineState(pso);
				ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

				Diligent::DrawAttribs draw;
				draw.NumVertices = 6;
				draw.NumInstances = last - first;
				draw.StartVertexLocation = 0;
				draw.FirstInstanceLocation = first;
				draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
				ctx->Draw(draw);
				g_ddDrawn++;
			}
		}
		first = last;
	}


	if (rtv) ctx->SetRenderTargets(1, &rtv, SceneDepthDSV(),
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

}} // namespace zx::hwrender
