// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Milestone 5: the actual Doom world, drawn through Diligent, and benchmarked against GL.
//
// The geometry comes straight from features/levelmesh -- the baked wall vertices the GL renderer
// already produces. That is the whole architectural claim of the level-mesh work made concrete: the
// backend consumes plain interleaved position+uv vertices and knows nothing about GLWall, draw
// lists, subsectors or the BSP.
//
// Deliberately untextured (shaded by height so the geometry is legible). Materials are a large
// subsystem of their own -- FMaterial, translations, warp/brightmap shaders -- and porting them is a
// later milestone. What this milestone answers is the question that decides whether the swap is
// worth continuing: **at real scene scale, how fast is the new backend at submitting this geometry
// compared to the one we have?**
//
// The comparison is honest because both draw the same vertices from the same viewpoint: `fua_diligent_scene`
// snapshots the baked mesh and the engine's current camera, then `fua_diligent_bench` redraws exactly
// that, so the number is API submission cost with the scene held fixed.

#include "c_dispatch.h"
#include "c_console.h"

#ifdef FUA_DILIGENT

#ifndef PLATFORM_WIN32
#define PLATFORM_WIN32 1
#endif

#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "Buffer.h"
#include "Texture.h"
#include "TextureView.h"
#include "Sampler.h"
#include "MapHelper.hpp"
// [rc4l] No #include "Query.h" here, deliberately: dxsdk/Include has a Query.h of its own that wins
// the search and drags in the reshaped windows headers (DWORD gets redefined). DeviceContext.h
// already pulls in Diligent's, since it declares BeginQuery/EndQuery over IQuery.
#include "RefCntAutoPtr.hpp"

#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/wallcache.h"   // LevelGeneration, for the per-level auto setup
#include "features/levelmesh/levelmesh.h"   // ArmFullBake
#include "d_main.h"                          // gamestate
#include "features/levelmesh/flatmesh.h"
#include "features/hwrender/hud2d.h"
#include "v_video.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/dynlights/gl_lightbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "p_local.h"
#include "doomstat.h"
#include "templates.h"   // gl_lightdata.h's inline gl_ClampLight uses clamp<>
#include "gl/renderer/gl_lightdata.h"
#include "gl/data/gl_data.h"
#include "gl/scene/gl_portal.h"
#include "gl/textures/gl_material.h"
#include "r_sky.h"
#include "textures/textures.h"
#include "g_level.h"
#include "ShaderResourceVariable.h"
#include "gl/data/gl_vertexbuffer.h"
#include "r_utility.h"
#include "r_state.h"
#include "doomtype.h"
#include "i_system.h"
#include <math.h>
#include <algorithm>
#include <stdio.h>
#include "m_png.h"
#include "doomtype.h"

// [rc4l] Declared outside the namespace: EXTERN_CVAR builds a name from the identifier, so a
// namespace-qualified one resolves to a symbol the engine never defines and fails only at link time.
EXTERN_CVAR(Int, gl_fogmode)
// [rc4l] fua_vulkan turns this on itself -- see AutoSetupForLevel.
EXTERN_CVAR(Bool, gl_wallmesh)
// [rc4l] The engine's texture filter mode; the backend mirrors it. See FillSamplerFromEngine.
EXTERN_CVAR(Int, gl_texture_filter)
EXTERN_CVAR(Float, gl_lights_size)
EXTERN_CVAR(Float, gl_lights_intensity)
EXTERN_CVAR(Bool, gl_lights_additive)

// [rc4l] 0 flat multiply (pre-lighting-port behaviour), 1 the ported equation, 2 depth as grey,
// 3 depth contours, 4 vertex colour only. Live per frame -- no rebake needed to switch.
CVAR(Int, fua_dg_lightmode, 1, CVAR_ARCHIVE)

// [rc4l] Mirror every engine frame into the backend window, from the live camera. Off by default:
// it costs a second render of the scene, and while the backend is incomplete (no sky, no HUD, no
// dynamic lights) it is a development view, not the game.
CVAR(Bool, fua_diligent_live, false, 0)
// [rc4l] One switch that means "render this game in Vulkan", as opposed to the three console
// commands and a fixed order that it used to take.
//
// Setting up the backend by hand was fine while the only thing anyone did with it was measure one
// map: bake, upload, go live, in that order, from a script. It falls apart the moment you change
// level -- the mesh is still the old map's, so the new one renders as the previous one's geometry --
// which is exactly what browsing a wad does forty times an hour. This re-arms itself per level.
CVAR(Bool, fua_vulkan, false, CVAR_ARCHIVE)

// [rc4l] Backface culling mode for the world: 0 none, 1 back, 2 front. DEFAULT 0, measured.
//
// Culling looks like free performance here and is not, yet: flats are triangle fans over a
// subsector's edges, and a floor and a ceiling built the same way have OPPOSITE winding, because one
// is viewed from above and the other from below. The engine draws flats with culling disabled for
// exactly this reason. Turning on CULL_MODE_BACK culled every floor on Sunder MAP10 and the sky
// poured through the hole -- the sampled floor patch went from (20.2, 16.0, 10.3), which matches
// GL's (20.8, 17.3, 12.4), to (173.5, 0.5, 0.2): solid red sky.
//
// Making this work means normalising winding at bake time (reverse the fan for ceilings, and check
// walls the same way), which is worth doing -- it would halve flat fragment work -- but it is an
// optimisation, not a prerequisite. The cvar stays so the experiment is one command away.
CVAR(Int, fua_dg_cull, 0, CVAR_ARCHIVE)

// [rc4l] Draw the sky. Off is a diagnostic: the sky fills every pixel the world does not cover, so
// with it on, "missing world geometry" and "correctly visible sky" look identical. Turning it off
// leaves holes against the clear colour, which is unambiguous.
CVAR(Bool, fua_dg_sky, true, 0)

// [rc4l] Re-resolve animated textures per frame. See the loop in DrawSceneOnce.
CVAR(Bool, fua_dg_animate, true, 0)

// [rc4l] Draw the captured 2D layer (HUD, menus, console) over the world.
CVAR(Bool, fua_dg_hud, true, 0)

// [rc4l] Dynamic lights: muzzle flashes, plasma, rocket trails, lamps.
CVAR(Bool, fua_dg_dynlights, true, 0)

// [rc4l] The camera pitch, which lives outside any header the backend already pulls in.
extern int viewpitch;

namespace zx { namespace hwrender {

Diligent::IRenderDevice  *GetDevice();
Diligent::ITextureView   *GetMaterialSRV(const void *materialPtr, int translation);
int MaterialCount();
bool MaterialIsMasked(const void *materialPtr);
Diligent::IDeviceContext *GetContext();
Diligent::ISwapChain     *GetSwapChain();
bool DiligentShowWindow(FString &report);
void  Fua_PumpBackendWindow(void *hwnd);
void  Fua_SyncBackendWindowToParent(void *hwnd);
void  Fua_ShowBackendWindow(void *hwnd, int visible);
void *GetBackendWindow();
void Fua_SetBackendWindowSize(int w, int h);

static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_vb;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_cb;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_scenePSO;      // opaque, early-Z intact
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_maskedPSO;     // alpha-tested
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_transPSO;      // normal translucency
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_addPSO;        // additive
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_srb;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_srbMasked;
static int   g_sceneVerts = 0;
static float g_mvp[16];
static int   g_drawRepeat = 1;

// [rc4l] One draw per material. Pieces arrive in bake order, so they are sorted by material first;
// with a few hundred materials on a map that is a few hundred draws, which is what the GL path does
// too once batched.
struct SceneBatch
{
	const void  *material;
	unsigned int first, count;
	bool         masked;
	// [rc4l] This batch's OWN resource binding -- see g_batchSRBs.
	Diligent::IShaderResourceBinding *srb;
	// [rc4l] Base texture and the frame we last resolved it to, so animated surfaces keep moving
	// without re-uploading geometry. NULL means "not animatable", which is most of a level.
	const void  *baseTex;
	const void  *resolved;
};
static TArray<SceneBatch> g_batches;

// [rc4l] Where each mesh piece landed in the backend's material-sorted vertex buffer, so geometry
// that moves can be re-uploaded in place. See RefreshMovedGeometry.
struct PieceMap { unsigned int meshOffset, count, vbOffset; };
static TArray<PieceMap> g_pieceMap;
static int g_geomUpdates = 0;
static unsigned int g_lastDirtyLo = 0, g_lastDirtyHi = 0;
// [rc4l] How many times the scene was rebuilt because the world moved, cumulative.
//
// Two matched screenshots of a lift agreed perfectly and proved nothing, because there was no way to
// tell whether the lift had moved at all in the frames between them. A counter answers "did the
// moving-geometry path even run" without reading pixels.
static int g_geomRebuilds = 0;

// [rc4l] One SRB per material, not one SRB re-pointed per draw.
//
// The draw loop used to do `var->Set(srv); CommitShaderResources(srb); Draw();` in a loop over a
// single shared SRB, which is a natural-looking way to write it and is wrong. A MUTABLE shader
// variable is baked into the SRB's descriptor set when the SRB is committed; re-setting it between
// draws inside one command buffer does not produce a new descriptor set, so every batch in the frame
// ends up sampling whichever texture the set last held. The whole world rendered in a single
// repeating texture -- and it looked *plausible*, because Doom levels reuse textures heavily.
//
// Ownership is explicit: ZDoom's TArray moves its storage with realloc, which is not safe for a
// refcounting smart pointer, so the SRBs are held as raw AddRef'd pointers and released by hand on
// re-upload. (Storing RefCntAutoPtr in a TArray crashed on the first upload.)
static TArray<Diligent::IShaderResourceBinding *> g_batchSRBs;

static void ReleaseBatchSRBs()
{
	for (unsigned i = 0; i < g_batchSRBs.Size(); i++)
		if (g_batchSRBs[i]) g_batchSRBs[i]->Release();
	g_batchSRBs.Clear();
}

// [rc4l] Material -> SRB, cached for the life of the level.
//
// The static path can build its SRBs once at upload, but the dynamic path (sprites) sees a different
// set of materials every frame, and creating SRBs per frame would allocate descriptor sets at 60 Hz.
// Keyed on the FMaterial pointer, which is stable per level.
// [rc4l] Keyed on (PSO, material), not material alone: an SRB is created from a pipeline and is only
// valid with it, so the translucent and additive passes need their own even for the same texture.
struct MatSRB { const void *material; const void *pso; int translation; Diligent::IShaderResourceBinding *srb; };
static TArray<MatSRB> g_matSRBs;

static Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso,
	const void *material, int translation = 0)
{
	if (!pso) return NULL;
	for (unsigned i = 0; i < g_matSRBs.Size(); i++)
		if (g_matSRBs[i].material == material && g_matSRBs[i].pso == pso &&
		    g_matSRBs[i].translation == translation) return g_matSRBs[i].srb;

	MatSRB e;
	e.material = material;
	e.pso = pso;
	e.translation = translation;
	e.srb = NULL;
	pso->CreateShaderResourceBinding(&e.srb, true);
	if (e.srb)
	{
		if (auto *v = e.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(GetMaterialSRV(material, translation));
	}
	g_matSRBs.Push(e);
	return e.srb;
}

static void ReleaseMaterialSRBs()
{
	for (unsigned i = 0; i < g_matSRBs.Size(); i++)
		if (g_matSRBs[i].srb) g_matSRBs[i].srb->Release();
	g_matSRBs.Clear();
}

// [rc4l] The dynamic light buffer, mirrored from the engine every frame.
//
// A structured (storage) buffer rather than a uniform block, matching the engine's own preferred
// path: the light list is variable-length and indexed per surface, which is exactly what a UBO is
// bad at. Grow-only, and uploaded once per engine frame -- the same generation guard the sprite
// stream uses, for the same reason (Vulkan's dynamic heap is per frame, not per draw).
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lightBuf;
static unsigned int g_lightBufCapacity = 0;   // in vec4s
static int g_lightCount = 0;
static bool g_lightBindFailed = false;

// [rc4l] The per-frame sprite geometry: uploaded fresh every frame, never cached.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_dynVB;
static unsigned int g_dynVBCapacity = 0;
static int g_dynDraws = 0, g_dynTris = 0;
// [rc4l] Per-blend-mode draw counts. Verifying translucency by screenshot means catching a
// projectile mid-flight; counting which pipelines actually ran is deterministic and takes a second.
static int g_dynByBlend[4] = { 0, 0, 0, 0 };
// [rc4l] Cumulative animation frame swaps, so "are animated textures actually animating" is a
// number rather than a squint at two screenshots.
static int g_animSwaps = 0;

// [rc4l] The backend's own vertex, laid out BY MATERIAL rather than in bake order.
//
// This is the material-keyed buffer the GL side could not profit from (see docs/levelmesh-PLAN.md,
// P2c/P2d) -- but here it is not an optimisation, it is a correctness requirement. Pieces sharing a
// material are scattered through the level mesh, so without reordering each piece needs its own
// draw: 4093 draws for Sunder MAP10, and 4093 constant-buffer maps per frame, which exhausts
// Vulkan's dynamic heap outright.
//
// Light is baked per vertex for the same reason: it removes the per-draw uniform update entirely, so
// the constant buffer is mapped once per frame instead of once per batch.
// [rc4l] The lighting parameters ride the vertex too, for the same reason the color does: the engine
// sets them per *draw* (uLightLevel, uFogDensity, uFogColor, uFogEnabled are uniforms), and a port
// that kept them as uniforms would be back to one draw per piece. They are constant across a piece,
// so duplicating them per vertex costs 24 bytes and buys a single draw per material.
struct SceneVertex
{
	float x, y, z;
	float u, v;
	float r, g, b;      // vColor -- gl_CalcLightColor(hwlight, LightColor, blendfactor)
	float softLight;    // uLightLevel: 0..1 in software lighting mode, -1 otherwise
	float fogDensity;   // uFogDensity: density * -log2(e)/64000, ready for exp2()
	float alpha;        // per-piece translucency; 1 for everything opaque
	float fogR, fogG, fogB;
	float fogMode;      // uFogEnabled: 0 off, +gl_fogmode plain, -gl_fogmode coloured
	float lightIndex;   // unused now the shader tests every light; kept for the vertex layout
	float nx, ny, nz;   // surface normal, for the dynamic-light side test
};
static TArray<SceneVertex> g_sceneVB;

// [rc4l] Same vertex layout as FFlatVertex (3 float position, 2 float uv), so the level mesh's data
// is uploaded byte-for-byte with no conversion pass.
static const char *kSceneVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec3 aColor;\n"
	"layout(location = 3) in vec3 aLightParm;\n"
	"layout(location = 4) in vec4 aFog;\n"
	"layout(location = 5) in float aLightIndex;\n"
	"layout(location = 6) in vec3 aNormal;\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; };\n"
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec3 vColor;\n"
	"layout(location = 2) out vec3 vLightParm;\n"
	"layout(location = 3) out vec4 vFog;\n"
	"layout(location = 4) out vec4 vPixelPos;\n"
	"layout(location = 5) flat out int vLightIndex;\n"
	"layout(location = 6) out vec3 vNormal;\n"
	"void main() {\n"
	"    vec4 clip = uMVP * vec4(aPos, 1.0);\n"
	"    gl_Position = clip;\n"
	"    vUV = aUV;\n"
	"    vColor = aColor;\n"
	"    vLightParm = aLightParm;\n"
	"    vFog = aFog;\n"
	// [rc4l] main.vp keeps pixelpos as (world xyz, view depth). The view depth is what the plane
	// fog mode measures along, and clip.w IS that depth for a standard perspective matrix -- the
	// projection's -1 in the w row copies eye-space -z straight through.
	"    vPixelPos = vec4(aPos, clip.w);\n"
	"    vLightIndex = int(aLightIndex);\n"
	"    vNormal = aNormal;\n"
	"}\n";

// [rc4l] The engine's real lighting, ported.
//
// This is a transcription of wadsrc/static/shaders/glsl/main.fp -- R_DoomLightingEquation and the
// getLightColor/applyFog pair -- not an approximation of it. Doom's light is not a multiply: it is a
// colormap *index* that darkens with distance, and the band structure that produces is most of what
// the game looks like. A flat multiply renders a world that is legible but visibly not Doom.
//
// The dist argument is gl_FragCoord.z exactly as the engine passes it, which is why the projection
// matrix had to be moved to Vulkan's [0,1] depth range first -- feeding a [-1,1] z into a 1/dist
// falloff would have produced light bands in the wrong places, and worse, plausible-looking ones.
//
// Ported here: software-mode light diminishing, fog-mode diminishing, coloured fog, both fog
// distance modes. Not ported: dynamic lights, glowing walls, brightmaps, desaturation, fixed
// colormaps (invulnerability et al). Those are separate uniforms//buffers rather than separate math.
//
// uCameraPos.w carries fua_dg_lightmode -- riding in the unused w of a vector already in the buffer
// makes the debug views free (no resize, no extra binding) so they can stay in the shipped shader.
// Two silent-failure bugs this port survived were caught only by looking at a picture; being able to
// ask the shader what it thinks depth and colour are is worth the three lines.
#define FUA_LIGHT_GLSL \
	"layout(location = 0) in vec2 vUV;\n" \
	"layout(location = 1) in vec3 vColor;\n" \
	"layout(location = 2) in vec3 vLightParm;\n" \
	"layout(location = 3) in vec4 vFog;\n" \
	"layout(location = 4) in vec4 vPixelPos;\n" \
	"layout(location = 5) flat in int vLightIndex;\n" \
	"layout(location = 6) in vec3 vNormal;\n" \
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; };\n" \
	"layout(binding = 1) uniform sampler2D uTex;\n" \
	"layout(std430, binding = 2) readonly buffer LightBuffer { vec4 lights[]; };\n" \
	"layout(location = 0) out vec4 outColor;\n" \
	/* [rc4l] Every light, tested per fragment -- no per-surface light list at all.

	   The engine builds a list of lights touching each surface and hands the shader an index into it.
	   That index is rebuilt EVERY FRAME, so it cannot live in a static mesh: baking it produced a
	   world where the dynamic lights simply never appeared, because every piece had recorded -1 on
	   the frame it was baked.

	   Rather than re-upload geometry per frame to fix the index, this drops the whole mechanism: the
	   buffer holds all active lights and the fragment shader tests each one. That machinery exists to
	   save GPU work, and the scale probe measured this GPU at 0.018 ms/frame for the entire visible
	   world -- roughly 0.1% utilisation -- so there is nothing to save. It is also the shape a
	   clustered forward renderer wants next.

	   Layout: pairs of (x, z, y, radius) and (r, g, b, mode), mode 1 meaning subtractive. Positions
	   use the same axis swap as the mesh vertices, so they compare directly against vPixelPos.xyz. */ \
	"vec3 fuaDynLight(vec3 base) {\n" \
	"    int n = int(uLightParams.x);\n" \
	"    if (n <= 0) return base;\n" \
	"    vec3 dyn = vec3(0.0);\n" \
	"    for (int i = 0; i < n; i++) {\n" \
	"        vec4 lp = lights[i*2];\n" \
	"        vec4 lc = lights[i*2+1];\n" \
	"        vec3 d = lp.xyz - vPixelPos.xyz;\n" \
	/* The side test gl_GetLight does per surface: a light behind the plane does not light it.
	   Without this the backs of walls and the room next door get lit, which reads as a scene far
	   more saturated than GL's. */ \
	"        if (dot(vNormal, d) <= 0.0) continue;\n" \
	"        float a = max(lp.w - length(d), 0.0) / lp.w;\n" \
	"        if (a <= 0.0) continue;\n" \
	"        if (lc.a > 0.5) dyn -= lc.rgb * a;\n" \
	"        else            dyn += lc.rgb * a;\n" \
	"    }\n" \
	"    return clamp(base + dyn, 0.0, 1.4);\n" \
	"}\n" \
	"float R_DoomLightingEquation(float light, float dist) {\n" \
	"    float L = light * 63.0/31.0;\n" \
	"    float min_L = clamp(36.0/31.0 - L, 0.0, 1.0);\n" \
	"    if (dist < 0.0001) dist = 0.0001;\n" \
	"    float scale = 1.0 / dist;\n" \
	"    float index = (59.0/31.0 - L) - (scale * 232.0/31.0 - 232.0/31.0);\n" \
	"    return clamp(index, min_L, 1.0);\n" \
	"}\n" \
	"vec3 fuaShade(vec3 texel) {\n" \
	"    float dbg = uCameraPos.w;\n" \
	"    if (dbg == 2.0) return vec3(gl_FragCoord.z);\n" \
	"    if (dbg == 3.0) return vec3(fract(gl_FragCoord.z * 64.0));\n" \
	"    if (dbg == 4.0) return vColor;\n" \
	"    if (dbg == 10.0) return texel;\n" \
	"    if (dbg == 11.0) return fuaDynLight(vec3(0.0));\n" \
	"    if (dbg == 12.0) return vec3(uLightParams.x / 16.0);\n" \
	"    if (dbg == 13.0) return vec3(lights[0].w / 256.0);\n" \
	"    if (dbg == 0.0) return texel * vColor;\n" \
	"    float fogMode = vFog.a;\n" \
	"    float fogdist = 0.0, fogfactor = 0.0;\n" \
	"    if (fogMode != 0.0) {\n" \
	"        fogdist = (abs(fogMode) < 1.5) ? vPixelPos.w\n" \
	"                                       : max(16.0, distance(vPixelPos.xyz, uCameraPos.xyz));\n" \
	"        fogfactor = exp2(vLightParm.y * fogdist);\n" \
	"    }\n" \
	"    if (dbg == 5.0) return vec3(fogfactor);\n" \
	"    if (dbg == 6.0) return vFog.rgb;\n" \
	"    if (dbg == 7.0) return vec3(clamp(vPixelPos.w / 2000.0, 0.0, 1.0));\n" \
	"    if (dbg == 8.0) return vec3(clamp(-vLightParm.y * 20000.0, 0.0, 1.0));\n" \
	"    if (dbg == 9.0) return vec3(fogMode > 0.0 ? 1.0 : 0.0, fogMode < 0.0 ? 1.0 : 0.0, 0.0);\n" \
	"    vec3 color = vColor;\n" \
	"    if (vLightParm.x >= 0.0) color *= 1.0 - R_DoomLightingEquation(vLightParm.x, gl_FragCoord.z);\n" \
	"    else if (fogMode > 0.0)  color  = mix(vec3(0.0), color, fogfactor);\n" \
	/* Dynamic lights add to the light COLOUR before the texture is modulated, exactly as
	   getLightColor does -- adding after would light the black parts of a texture too. */ \
	"    color = fuaDynLight(min(color, vec3(1.0)));\n" \
	"    vec3 frag = texel * color;\n" \
	"    if (fogMode < 0.0) frag = mix(vFog.rgb, frag, fogfactor);\n" \
	"    return frag;\n" \
	"}\n"

// [rc4l] Opaque: no discard, so early-Z survives and overdrawn fragments are never shaded.
static const char *kScenePSOpaque =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    outColor = vec4(fuaShade(texture(uTex, vUV).rgb), 1.0);\n"
	"}\n";

static const char *kScenePS =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    vec4 t = texture(uTex, vUV);\n"
	"    if (t.a < 0.5) discard;\n"
	"    outColor = vec4(fuaShade(t.rgb), 1.0);\n"
	"}\n";

// [rc4l] Translucent: no alpha test, and the texture's own alpha multiplied by the piece's. The
// blend factors live in the pipeline, not here, so this one shader serves both the normal and the
// additive pass -- additive differs only in its destination factor.
static const char *kScenePSTrans =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    vec4 t = texture(uTex, vUV);\n"
	"    if (t.a < 0.04) discard;\n"
	"    outColor = vec4(fuaShade(t.rgb), t.a * vLightParm.z);\n"
	"}\n";

// [rc4l] Column-major perspective * view, matching the GL renderer's convention in
// FGLRenderer::SetProjection/SetViewMatrix: X east, Y up, Z south, with the pixel-stretch flip.
static void BuildMVP(float *m)
{
	const float fovY = 74.0f * 3.14159265f / 180.0f;
	// [rc4l] From the swapchain, not a constant. A hard-coded 16:10 meant the backend framed the
	// world differently from the engine window, so a screenshot pair could never be compared
	// pixel-for-pixel -- the same screen position was a different part of the level in each.
	float aspect = 16.0f / 10.0f;
	if (auto *swap = GetSwapChain())
	{
		const auto &sd = swap->GetDesc();
		if (sd.Height > 0) aspect = (float)sd.Width / (float)sd.Height;
	}
	const float zn = 5.0f, zf = 65536.0f;
	const float f = 1.0f / tanf(fovY * 0.5f);

	// [rc4l] Vulkan clip space maps depth to [0,1], not GL's [-1,1]. The GL form was mostly getting
	// away with it -- only 5..10 map units fell behind the near plane -- but it makes gl_FragCoord.z
	// mean something different from what it means in the engine's shaders, and the ported lighting
	// equation reads gl_FragCoord.z directly. Getting this right is what makes the light bands land
	// where the GL renderer puts them.
	float p[16] = {0};
	p[0] = f / aspect; p[5] = f; p[10] = zf / (zn - zf); p[11] = -1.0f;
	p[14] = (zf * zn) / (zn - zf);

	// [rc4l] A proper look-at, derived rather than fitted.
	//
	// This used to build the rotation from `ry = 270 - yaw`, which makes the error scale with 2*yaw:
	// correct at yaw 0 and 180 degrees out at yaw 90. Sunder MAP10's player start happens to face
	// angle 0, so every screenshot comparison taken there matched pixel-for-pixel and the bug stayed
	// invisible -- until Doom 2 MAP01, which starts facing north, rendered a completely different
	// room from GL while both were paused on the same frame.
	//
	// Doom angles: 0 is east (+x), 90 is north (+y). The mesh is (x, z-up, y) -- that is
	// (east, up, north), which is a LEFT-handed basis: east x up = -north.
	//
	// So a textbook right-handed look-at is wrong here, and wrong in a way that is easy to miss: it
	// mirrors the world horizontally. A plain `s = cross(f, up)` put the camera's right hand to the
	// west, and the entire scene came out flipped -- which reads as "the sprites are backwards"
	// because a mirrored monster is the most recognisable thing on screen. Mean-colour comparisons
	// against GL did not catch it either: mirroring a roughly symmetric corridor barely moves the
	// average.
	//
	// The right basis for this handedness is s = cross(up, f):
	//     s    = cross(up, f) = ( sin a, 0, -cos a)   facing north, right is east
	//     u    =               = ( 0,     1,  0)
	//     row2 = -f            = (-cos a, 0, -sin a)
	// Its determinant is -1, which is exactly the reflection needed to take left-handed mesh space
	// into the right-handed eye space the projection expects.
	const float a = (float)(viewangle >> ANGLETOFINESHIFT) * 2.0f * 3.14159265f / 8192.0f;
	const float ca = cosf(a), sa = sinf(a);

	// [rc4l] Pitch, derived exactly the way FGLRenderer::SetupView derives it.
	//
	// There was no pitch term here at all: the matrix was built from yaw alone, so looking up or down
	// moved the engine's view and left the backend's staring at the horizon. It read as "freelook is
	// broken" rather than "the backend ignores one axis", because everything else tracked perfectly.
	//
	// The pixelstretch correction is not cosmetic. The playsim treats pixels as square and the
	// renderer does not, so the pitch the camera should use is not the pitch the actor has -- copying
	// the raw angle would drift from GL by several degrees at the extremes, which is exactly the kind
	// of near-miss that survives a screenshot comparison and shows up later as "the aim is off".
	double radPitch = bam2rad((angle_t)viewpitch);
	if (radPitch > PI) radPitch -= 2 * PI;
	// Not clamp(): inside this namespace that resolves to the Fixed-point overload.
	if (radPitch < -PI / 2) radPitch = -PI / 2;
	if (radPitch >  PI / 2) radPitch =  PI / 2;
	const double angx = cos(radPitch);
	const double angy = sin(radPitch) * glset.pixelstretch;
	const double alen = sqrt(angx * angx + angy * angy);
	const float pitch = (alen > 0.0) ? (float)asin(angy / alen) : 0.0f;
	const float cp = cosf(pitch), sp = sinf(pitch);

	const float px = FIXED2FLOAT(viewx), py = FIXED2FLOAT(viewz), pz = FIXED2FLOAT(viewy);

	// Column-major: v[col*4 + row].
	float v[16] = {0};
	// Rows 1 and 2 are the yaw-only u and -f rotated about row 0 by the pitch, which is the same
	// composition GL does as rotate(Pitch, 1,0,0) * rotate(Yaw, 0,1,0).
	v[0] =  sa;      v[4] = 0.0f; v[8]  = -ca;       // row 0 = s = cross(up, f), unaffected by pitch
	v[1] =  sp * ca; v[5] = cp;   v[9]  =  sp * sa;  // row 1 = cos(p)*u + sin(p)*f
	v[2] = -cp * ca; v[6] = sp;   v[10] = -cp * sa;  // row 2 = sin(p)*u - cos(p)*f
	v[15] = 1.0f;
	v[12] = -(v[0]*px + v[4]*py + v[8]*pz);
	v[13] = -(v[1]*px + v[5]*py + v[9]*pz);
	v[14] = -(v[2]*px + v[6]*py + v[10]*pz);

	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
		{
			float sum = 0;
			for (int k = 0; k < 4; k++) sum += p[k*4 + r] * v[c*4 + k];
			m[c*4 + r] = sum;
		}
}
// ---------------------------------------------------------------------------
// Sky
// ---------------------------------------------------------------------------
//
// [rc4l] Without this the world sits under a flat void, which is most of what "it's not all of it"
// looks like: sky is not geometry in a Doom level, it is the *absence* of it. Sky walls and sky
// flats are handed to the portal manager and never reach the mesh, so wherever the map opens to sky
// the backend simply drew nothing.
//
// The dome is a transcription of FSkyVertexBuffer::SkyVertex (gl/scene/gl_skydome.cpp) -- same
// maxSideAngle, same 10000-unit radius, same mirrored X, same +300 nudge on the non-horizon rows,
// same UV convention. The per-texture-height model transform from RenderDome is baked into the
// vertices instead of being a matrix, since it only changes when the sky texture does.
//
// Drawn first with depth test and depth write OFF, so it fills the frame and the world paints over
// it. That is exactly Doom's rule -- sky is whatever is behind everything else -- and it means sky
// flats need no special handling at all: nothing is drawn there, so the sky survives.
struct SkyVertexData { float x, y, z, u, v; };

static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skySRB;
static int          g_skyVerts = 0;
static const void  *g_skyMaterial = NULL;
static int          g_skyBuiltFor = -1;   // FTextureID index; == has ambiguous overloads
static bool         g_skyBuiltValid = false;

static const char *kSkyVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; };\n"
	"layout(location = 0) out vec2 vUV;\n"
	"void main() {\n"
	// Centred on the camera, so the dome is effectively at infinity.
	"    gl_Position = uMVP * vec4(aPos + uCameraPos.xyz, 1.0);\n"
	"    vUV = aUV;\n"
	"}\n";

static const char *kSkyPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(uTex, vUV).rgb, 1.0); }\n";

static void SkyVertexAt(int r, int c, int rows, int cols, bool yflip, float yscale,
	float xscale, SkyVertexData &out)
{
	const float maxSideAngle = 60.0f * 3.14159265f / 180.0f;   // ANGLE_180/3
	const float scale = 10000.0f;

	const float topAngle  = (float)c / cols * 2.0f * 3.14159265f;
	const float sideAngle = maxSideAngle * (float)(rows - r) / rows;
	const float height    = sinf(sideAngle);
	const float realRadius = scale * cosf(sideAngle);

	float x = realRadius * cosf(topAngle);
	float y = yflip ? -(scale * height) : (scale * height);
	float z = realRadius * sinf(topAngle);
	if (r != rows) y += 300.0f;

	out.x = -x;                       // Doom mirrors the sky horizontally
	out.y = y * yscale - 1250.0f;     // RenderDome's translate+scale, baked in
	out.z = z;
	out.u = (-(float)c / cols) * xscale;
	out.v = yflip ? (1.0f + (float)(rows - r) / rows) : ((float)r / rows);
}

static void BuildSkyDome(int texw, int texh)
{
	const int rows = 4, cols = 32;

	// RenderDome's per-height cases. Doom's own skies are 256x128, which lands in the middle branch.
	float yscale = 1.0f;
	if (texh < 128)      yscale = 128 / 230.f;
	else if (texh < 200) yscale = texh / 230.f;
	else if (texh <= 240) yscale = texh / 230.f;
	const float xscale = texw > 0 ? 1024.f / texw : 1.f;

	TArray<SkyVertexData> verts;
	for (int hemi = 0; hemi < 2; hemi++)
	{
		const bool yflip = (hemi == 1);
		for (int r = 0; r < rows; r++)
		{
			for (int c = 0; c < cols; c++)
			{
				SkyVertexData q[4];
				SkyVertexAt(r,     c,     rows, cols, yflip, yscale, xscale, q[0]);
				SkyVertexAt(r + 1, c,     rows, cols, yflip, yscale, xscale, q[1]);
				SkyVertexAt(r,     c + 1, rows, cols, yflip, yscale, xscale, q[2]);
				SkyVertexAt(r + 1, c + 1, rows, cols, yflip, yscale, xscale, q[3]);
				verts.Push(q[0]); verts.Push(q[1]); verts.Push(q[2]);
				verts.Push(q[2]); verts.Push(q[1]); verts.Push(q[3]);
			}
		}
	}

	g_skyVerts = (int)verts.Size();
	if (g_skyVerts == 0) return;

	Diligent::BufferDesc bd;
	bd.Name = "fua sky VB";
	bd.Size = (Diligent::Uint64)g_skyVerts * sizeof(SkyVertexData);
	bd.Usage = Diligent::USAGE_IMMUTABLE;
	bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	Diligent::BufferData bdata;
	bdata.pData = &verts[0];
	bdata.DataSize = bd.Size;
	g_skyVB.Release();
	GetDevice()->CreateBuffer(bd, &bdata, &g_skyVB);
}

static bool EnsureSkyPipeline()
{
	if (g_skyPSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
		ci.Desc.Name = "fua sky VS";
		ci.Source = kSkyVS;
		dev->CreateShader(ci, &vs);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua sky PS";
		ci.Source = kSkyPS;
		dev->CreateShader(ci, &ps);
	}
	if (!vs || !ps) return false;

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
	};
	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	samp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
	samp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
	samp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;   // no wrap top-to-bottom
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
	};

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua sky PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	// The sky is behind everything: never tested, never written.
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = 2;
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = 1;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	pci.pVS = vs;
	pci.pPS = ps;
	dev->CreateGraphicsPipelineState(pci, &g_skyPSO);
	if (!g_skyPSO) return false;

	if (auto *v = g_skyPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants"))
		v->Set(g_cb);
	g_skyPSO->CreateShaderResourceBinding(&g_skySRB, true);
	// RefCntAutoPtr has several conversions, so `!= NULL` is ambiguous; ask it directly.
	return g_skySRB.RawPtr() != nullptr;
}

// Rebuild the dome and its binding when the level's sky texture changes.
static void EnsureSky()
{
	if (!EnsureSkyPipeline()) return;
	if (g_skyBuiltValid && g_skyBuiltFor == sky1texture.GetIndex()) return;

	FTextureID id = sky1texture;
	FMaterial *mat = id.isValid() ? FMaterial::ValidateTexture(TexMan[id], false) : NULL;
	if (mat == NULL) { g_skyBuiltValid = false; return; }

	BuildSkyDome(mat->TextureWidth(), mat->TextureHeight());
	g_skyMaterial = mat;
	if (auto *v = g_skySRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(GetMaterialSRV(g_skyMaterial, 0));
	g_skyBuiltFor = id.GetIndex();
	g_skyBuiltValid = true;
}

static void DrawSky(Diligent::IDeviceContext *ctx)
{
	if (!g_skyBuiltValid || !g_skyVB || !g_skyPSO || g_skyVerts == 0) return;

	Diligent::IBuffer *vbs[] = { g_skyVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_skyPSO);
	ctx->CommitShaderResources(g_skySRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	Diligent::DrawAttribs draw;
	draw.NumVertices = (Diligent::Uint32)g_skyVerts;
	draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(draw);
}

// [rc4l] Draw this frame's dynamic geometry -- sprites.
//
// Rebuilt and re-uploaded every frame on purpose: billboards are view-dependent, so there is nothing
// to cache. The cost is bounded by what is on screen (a few hundred quads), and at ~5.6M triangles
// per GPU millisecond the upload dominates the draw by a wide margin.
//
// Drawn AFTER the world with the alpha-tested pipeline and normal depth, so sprites occlude and are
// occluded correctly. Translucent render styles are not handled yet -- they need a back-to-front
// sort and a blend state, which is the next milestone rather than something to fake here.
static void DrawDynamic(Diligent::IDeviceContext *ctx)
{
	g_dynDraws = g_dynTris = 0;
	g_dynByBlend[0] = g_dynByBlend[1] = g_dynByBlend[2] = g_dynByBlend[3] = 0;

	int nverts = 0, npieces = 0;
	const FFlatVertex *src = zx::levelmesh::DynVertices(nverts);
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(npieces);
	if (src == NULL || nverts <= 0 || pieces == NULL || npieces <= 0) return;

	// [rc4l] A run is one draw: a contiguous span sharing a material AND a blend mode.
	struct Run { const void *material; unsigned first, count; int blend; };
	static TArray<SceneVertex> vb;
	static TArray<Run> runs;

	// [rc4l] Rebuild and re-upload ONCE per engine frame, not once per draw call.
	//
	// DrawSceneOnce is called many times between frame boundaries by the benchmark, and Vulkan's
	// dynamic heap is only recycled when a frame completes -- so mapping here unconditionally
	// exhausted an 8 MB heap after a few hundred draws ("Space in dynamic heap is exhausted").
	static unsigned int builtGen = 0;
	const unsigned int gen = zx::levelmesh::DynGeneration();
	if (gen != builtGen)
	{
		builtGen = gen;

		// [rc4l] Order decides correctness here, not just batching.
		//
		// Opaque pieces first, grouped by material so they batch. Translucent pieces after, sorted
		// BACK TO FRONT by distance from the camera -- blending is not commutative, so two overlapping
		// translucent sprites drawn in the wrong order composite wrongly. Within the translucent set
		// the sort beats batching, so material grouping is deliberately given up there.
		static TArray<int> order;
		order.Clear();
		for (int i = 0; i < npieces; i++) order.Push(i);

		const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);
		static TArray<float> key;
		key.Clear();
		key.Resize(npieces);
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[i];
			const float dx = p.sortX - cx, dy = p.sortY - cy, dz = p.sortZ - cz;
			key[i] = dx*dx + dy*dy + dz*dz;
		}
		for (int a = 0; a + 1 < npieces; a++)
			for (int b = a + 1; b < npieces; b++)
			{
				const zx::levelmesh::MeshPiece &pa = pieces[order[a]];
				const zx::levelmesh::MeshPiece &pb = pieces[order[b]];
				bool swap;
				const bool ta = pa.blendMode != 0, tb = pb.blendMode != 0;
				if (ta != tb)            swap = tb < ta;                       // opaque before blended
				else if (!ta)            swap = pb.material < pa.material;     // opaque: by material
				else                     swap = key[order[b]] > key[order[a]]; // blended: far first
				if (swap) { const int t = order[a]; order[a] = order[b]; order[b] = t; }
			}

		vb.Clear();
		runs.Clear();
		const void *cur = (const void *)(size_t)-1;
		int curBlend = -1;
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[order[i]];
			if (p.range.count == 0) continue;
			// A translucent piece never merges with its neighbour: merging would reorder it.
			if (p.material != cur || p.blendMode != curBlend || p.blendMode != 0)
			{
				Run r; r.material = p.material; r.first = vb.Size(); r.count = 0; r.blend = p.blendMode;
				runs.Push(r);
				cur = p.material;
				curBlend = p.blendMode;
			}
			for (unsigned v = 0; v < p.range.count; v++)
			{
				const FFlatVertex &sv = src[p.range.offset + v];
				SceneVertex dv;
				dv.x = sv.x; dv.y = sv.z; dv.z = sv.y;
				dv.u = sv.u; dv.v = sv.v;
				dv.r = p.colorR; dv.g = p.colorG; dv.b = p.colorB;
				dv.softLight = p.softLight;
				dv.fogDensity = p.fogDensity;
				dv.alpha = p.alpha;
				dv.fogR = ((p.fogColor >> 16) & 0xff) / 255.f;
				dv.fogG = ((p.fogColor >> 8) & 0xff) / 255.f;
				dv.fogB = (p.fogColor & 0xff) / 255.f;
				dv.fogMode = (float)p.fogMode;
			dv.lightIndex = (float)p.dynLightIndex;
			dv.nx = p.normX; dv.ny = p.normY; dv.nz = p.normZ;
				vb.Push(dv);
			}
			runs[runs.Size() - 1].count += p.range.count;
		}
		if (vb.Size() == 0) return;

		// Grow-only, and USAGE_DEFAULT + UpdateBuffer rather than a mapped dynamic buffer: this data
		// changes once per frame, so it does not belong in the dynamic heap at all.
		if (!g_dynVB || g_dynVBCapacity < vb.Size())
		{
			g_dynVBCapacity = vb.Size() + vb.Size() / 2 + 1024;
			Diligent::BufferDesc bd;
			bd.Name = "fua dynamic VB";
			bd.Size = (Diligent::Uint64)g_dynVBCapacity * sizeof(SceneVertex);
			bd.Usage = Diligent::USAGE_DEFAULT;
			bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
			g_dynVB.Release();
			GetDevice()->CreateBuffer(bd, nullptr, &g_dynVB);
			if (!g_dynVB) { g_dynVBCapacity = 0; return; }
		}
		ctx->UpdateBuffer(g_dynVB, 0, (Diligent::Uint64)vb.Size() * sizeof(SceneVertex), &vb[0],
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	if (vb.Size() == 0 || !g_dynVB) return;

	Diligent::IBuffer *vbs[] = { g_dynVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);

	Diligent::IPipelineState *bound = NULL;
	for (unsigned i = 0; i < runs.Size(); i++)
	{
		if (runs[i].count == 0) continue;

		// Blend mode 3 is the fuzz shadow; the engine draws it as a dark near-opaque overlay, and
		// normal translucency is a fair stand-in until the fuzz shaders are ported.
		Diligent::IPipelineState *pso =
			(runs[i].blend == 0) ? g_maskedPSO.RawPtr() :
			(runs[i].blend == 2) ? g_addPSO.RawPtr()    : g_transPSO.RawPtr();
		if (!pso) continue;

		auto *srb = GetMaterialSRB(pso, runs[i].material);
		if (!srb) continue;

		if (pso != bound) { ctx->SetPipelineState(pso); bound = pso; }
		ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::DrawAttribs draw;
		draw.NumVertices = runs[i].count;
		draw.StartVertexLocation = runs[i].first;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
		g_dynDraws++;
		g_dynTris += runs[i].count / 3;
		if (runs[i].blend >= 0 && runs[i].blend < 4) g_dynByBlend[runs[i].blend]++;
	}
}

// [rc4l] Drop the pipelines and everything bound to them, so the next upload rebuilds with current
// settings. SRBs are created from a PSO and cannot outlive it.
static int g_builtCull = -1;
static int g_builtFilter = -1;
static void ReleaseScenePipelines()
{
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	for (unsigned i = 0; i < g_batches.Size(); i++) g_batches[i].srb = NULL;
	g_srb.Release();
	g_srbMasked.Release();
	g_skySRB.Release();
	g_skyPSO.Release();
	g_scenePSO.Release();
	g_maskedPSO.Release();
	g_transPSO.Release();
	g_addPSO.Release();
	g_skyBuiltValid = false;
}

// ---------------------------------------------------------------------------
// 2D / HUD
// ---------------------------------------------------------------------------
//
// [rc4l] The status bar, weapon, messages, menus and console, from the quad list features/hwrender
// captured off the engine's own 2D path (see hud2d.h). Its own pipeline because 2D is a different
// problem from 3D: orthographic, no depth at all, painter's order, and blended throughout.
//
// Submission order is draw order and must not be reordered -- no material batching here. A 2D layer
// sorted by texture would put the status bar behind the world and the console behind the menu.
struct Vertex2D { float x, y, u, v, r, g, b, a, texMode; };

// [rc4l] One 2D pipeline per blend mode: 0 alpha, 1 additive, 2 multiply.
//
// There used to be one, always alpha, and Quad2D::blend was recorded and then ignored -- so additive
// 2D drew as ordinary alpha. Multiply is what a fixed colormap needs (invulnerability tints the
// screen by multiplying it), and neither can be faked with the alpha state.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pso2D[3];
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_vb2D;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_cb2D;
static unsigned int g_vb2DCapacity = 0;
static int g_draws2D = 0, g_quads2D = 0;

static const char *k2DVS =
	"#version 450\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aColor;\n"
	"layout(location = 3) in float aTexMode;\n"
	"layout(binding = 0) uniform Screen { vec4 uInvScreen; };\n"
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec4 vColor;\n"
	"layout(location = 2) flat out int vTexMode;\n"
	"void main() {\n"
	// Engine 2D space is ortho(0, W, H, 0): origin top-left, Y down. The Y is INVERTED here rather
	// than passed through, because Diligent's Vulkan backend renders with a negative-height viewport
	// so that GL- and D3D-style projections both come out upright -- which means the shader has to
	// hand it GL-style NDC (+1 at the top). Passing Vulkan-native NDC put the status bar along the
	// top of the screen, mirrored.
	"    vec2 ndc = vec2(aPos.x * uInvScreen.x * 2.0 - 1.0, 1.0 - aPos.y * uInvScreen.y * 2.0);\n"
	"    gl_Position = vec4(ndc, 0.0, 1.0);\n"
	"    vUV = aUV;\n"
	"    vColor = aColor;\n"
	"    vTexMode = int(aTexMode);\n"
	"}\n";

// [rc4l] The texture modes from gl_interface.h's TexMode, transcribed from getTexel() in main.fp.
//
// TM_MASK is the one that matters: coloured text is drawn with the glyph as a pure coverage MASK and
// the vertex colour supplying RGB. Multiplying by the texel instead -- which is what a naive
// `t * vColor` does -- renders every font muddy and dark, and it looks like a palette problem rather
// than a texture-mode one. The mode is captured per quad off the render state.
static const char *k2DPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(location = 1) in vec4 vColor;\n"
	"layout(location = 2) flat in int vTexMode;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    vec4 t = texture(uTex, vUV);\n"
	"    if (vTexMode == 1)      t.rgb = vec3(1.0);\n"                       // TM_MASK
	"    else if (vTexMode == 2) t.a = 1.0;\n"                               // TM_OPAQUE
	"    else if (vTexMode == 3) t = vec4(1.0-t.r, 1.0-t.b, 1.0-t.g, t.a);\n"// TM_INVERSE
	"    else if (vTexMode == 4) t = vec4(1.0, 1.0, 1.0, t.r);\n"            // TM_REDTOALPHA
	"    else if (vTexMode == 5 && (vUV.t < 0.0 || vUV.t > 1.0)) t.a = 0.0;\n"// TM_CLAMPY
	"    outColor = t * vColor;\n"
	"}\n";

static bool Ensure2DPipeline()
{
	if (g_pso2D[0]) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
		ci.Desc.Name = "fua 2D VS";
		ci.Source = k2DVS;
		dev->CreateShader(ci, &vs);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua 2D PS";
		ci.Source = k2DPS;
		dev->CreateShader(ci, &ps);
	}
	if (!vs || !ps) return false;

	Diligent::BufferDesc cbd;
	cbd.Name = "fua 2D constants";
	cbd.Size = sizeof(float) * 4;
	cbd.Usage = Diligent::USAGE_DYNAMIC;
	cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
	cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
	dev->CreateBuffer(cbd, nullptr, &g_cb2D);
	if (!g_cb2D) return false;

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
	};
	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	// [rc4l] CLAMP, and no mips: the engine draws 2D with CLAMP_XY_NOMIP. Wrapping would bleed the
	// opposite edge of a font glyph into its neighbour, and a mipped status bar goes soft.
	samp.MinFilter = Diligent::FILTER_TYPE_POINT;
	samp.MagFilter = Diligent::FILTER_TYPE_POINT;
	samp.MipFilter = Diligent::FILTER_TYPE_POINT;
	samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
	};

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua 2D PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
		rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
	}
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = 4;
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = 1;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	pci.pVS = vs;
	pci.pPS = ps;
	for (int mode = 0; mode < 3; mode++)
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		if (mode == 2)
		{
			// GL_DST_COLOR / GL_ZERO -- the screen multiplied by the blend colour.
			rt.SrcBlend  = Diligent::BLEND_FACTOR_DEST_COLOR;
			rt.DestBlend = Diligent::BLEND_FACTOR_ZERO;
		}
		else
		{
			rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
			rt.DestBlend = (mode == 1) ? Diligent::BLEND_FACTOR_ONE
			                           : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		}
		dev->CreateGraphicsPipelineState(pci, &g_pso2D[mode]);
		if (!g_pso2D[mode]) return false;
		if (auto *v = g_pso2D[mode]->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Screen"))
			v->Set(g_cb2D);
	}
	return true;
}

static void Draw2D(Diligent::IDeviceContext *ctx)
{
	g_draws2D = g_quads2D = 0;
	if (!fua_dg_hud) return;

	int nq = 0;
	const zx::hwrender::Quad2D *quads = zx::hwrender::Quads2D(nq);
	if (quads == NULL || nq <= 0) return;
	if (!Ensure2DPipeline()) return;

	int scrW = 0, scrH = 0;
	zx::hwrender::GetScreen2D(scrW, scrH);
	if (scrW <= 0 || scrH <= 0) return;

	// [rc4l] Rebuilt once per engine frame, gated on the generation counter -- same reason as the
	// sprite stream: DrawSceneOnce runs many times between frame boundaries and Vulkan's dynamic heap
	// is per frame, not per draw.
	static TArray<Vertex2D> vb;
	static unsigned int built2D = 0;
	const unsigned int gen = zx::hwrender::Generation2D();
	if (gen != built2D)
	{
		built2D = gen;
		vb.Clear();
		for (int i = 0; i < nq; i++)
		{
			const zx::hwrender::Quad2D &q = quads[i];
			Vertex2D v[4];
			v[0].x = q.x;       v[0].y = q.y;       v[0].u = q.u1; v[0].v = q.v1;
			v[1].x = q.x;       v[1].y = q.y + q.h; v[1].u = q.u1; v[1].v = q.v2;
			v[2].x = q.x + q.w; v[2].y = q.y;       v[2].u = q.u2; v[2].v = q.v1;
			v[3].x = q.x + q.w; v[3].y = q.y + q.h; v[3].u = q.u2; v[3].v = q.v2;
			for (int k = 0; k < 4; k++)
			{ v[k].r = q.r; v[k].g = q.g; v[k].b = q.b; v[k].a = q.a; v[k].texMode = (float)q.texMode; }
			vb.Push(v[0]); vb.Push(v[1]); vb.Push(v[2]);
			vb.Push(v[2]); vb.Push(v[1]); vb.Push(v[3]);
		}
		if (vb.Size() == 0) return;

		if (!g_vb2D || g_vb2DCapacity < vb.Size())
		{
			g_vb2DCapacity = vb.Size() + vb.Size() / 2 + 1024;
			Diligent::BufferDesc bd;
			bd.Name = "fua 2D VB";
			bd.Size = (Diligent::Uint64)g_vb2DCapacity * sizeof(Vertex2D);
			bd.Usage = Diligent::USAGE_DEFAULT;
			bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
			g_vb2D.Release();
			GetDevice()->CreateBuffer(bd, nullptr, &g_vb2D);
			if (!g_vb2D) { g_vb2DCapacity = 0; return; }
		}
		ctx->UpdateBuffer(g_vb2D, 0, (Diligent::Uint64)vb.Size() * sizeof(Vertex2D), &vb[0],
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	if (vb.Size() == 0 || !g_vb2D) return;

	{
		Diligent::MapHelper<float> cb(ctx, g_cb2D, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		cb[0] = 1.0f / scrW; cb[1] = 1.0f / scrH; cb[2] = 0.f; cb[3] = 0.f;
	}

	Diligent::IBuffer *vbs[] = { g_vb2D };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	// The pipeline is chosen per quad now, so this only primes it.
	int curBlend = -1;

	// The backend window is its own size, so the engine's scissor rects scale with the viewport.
	const auto &sd = GetSwapChain()->GetDesc();
	const float sx = (float)sd.Width / scrW, sy = (float)sd.Height / scrH;

	const void *curMat = (const void *)(size_t)-1;
	int curTrans = -99999;
	int cl = -1, ctp = -1, cr = -1, cb2 = -1;
	for (int i = 0; i < nq; i++)
	{
		const zx::hwrender::Quad2D &q = quads[i];
		const int blendMode = (q.blend >= 0 && q.blend < 3) ? q.blend : 0;
		if (blendMode != curBlend)
		{
			curBlend = blendMode;
			ctx->SetPipelineState(g_pso2D[curBlend]);
		}
		auto *srb = GetMaterialSRB(g_pso2D[curBlend], q.material, q.translation);
		if (!srb) continue;

		if (q.clipL != cl || q.clipT != ctp || q.clipR != cr || q.clipB != cb2)
		{
			cl = q.clipL; ctp = q.clipT; cr = q.clipR; cb2 = q.clipB;
			Diligent::Rect sc;
			sc.left   = (Diligent::Int32)(cl * sx);
			sc.top    = (Diligent::Int32)(ctp * sy);
			sc.right  = (Diligent::Int32)(cr * sx);
			sc.bottom = (Diligent::Int32)(cb2 * sy);
			if (sc.right <= sc.left || sc.bottom <= sc.top)
			{ sc.left = 0; sc.top = 0; sc.right = (Diligent::Int32)sd.Width; sc.bottom = (Diligent::Int32)sd.Height; }
			ctx->SetScissorRects(1, &sc, sd.Width, sd.Height);
		}

		// Commit on every change of EITHER material or translation -- the same glyph in two
		// colours is two different textures.
		if (q.material != curMat || q.translation != curTrans)
		{
			ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			curMat = q.material;
			curTrans = q.translation;
		}

		Diligent::DrawAttribs draw;
		draw.NumVertices = 6;
		draw.StartVertexLocation = (Diligent::Uint32)i * 6;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
		g_draws2D++;
	}
	g_quads2D = nq;
}

// [rc4l] Sample textures the way the engine's own renderer is configured to, not the way that looks
// best in isolation.
//
// This used to be hard-wired trilinear-with-point-mag, under a comment claiming that was "the same
// shape the GL renderer uses". It is not: gl_texture_filter defaults to 0, which is GL_NEAREST with
// mipmapping off -- Doom's crunchy look. Filtering the Vulkan view instead put a difference on every
// texture detail edge in the frame, which is diffuse rather than localised and therefore reads as
// noise rather than a bug. It dominated the GL-vs-Vulkan measurement and hid whatever real feature
// gaps sit underneath it.
//
// The five modes below are TexFilter[] in gl_texture.cpp, entry for entry. Modes 0 and 2 have
// mipmapping off, which a sampler expresses by refusing to sample past mip 0.
static void FillSamplerFromEngine(Diligent::SamplerDesc &samp)
{
	const int mode = (gl_texture_filter >= 0 && gl_texture_filter <= 5) ? (int)gl_texture_filter : 0;
	const bool magLinear = (mode == 2 || mode == 3 || mode == 4);
	const bool minLinear = (mode == 2 || mode == 3 || mode == 4);
	const bool mipLinear = (mode == 4 || mode == 5);
	const bool mipmapped = (mode != 0 && mode != 2);

	samp.MinFilter = minLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MagFilter = magLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MipFilter = mipLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MaxLOD = mipmapped ? 1000.0f : 0.0f;
}

static bool EnsureScenePipeline(FString &err)
{
	// The sampler is immutable in the PSO, so a filter change means a new PSO. Cheap and rare.
	if (g_scenePSO && g_builtFilter != (int)gl_texture_filter) ReleaseScenePipelines();
	g_builtFilter = (int)gl_texture_filter;
	if (g_scenePSO && g_builtCull != (int)fua_dg_cull) ReleaseScenePipelines();
	g_builtCull = (int)fua_dg_cull;
	if (g_scenePSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) { err = "no device/swapchain"; return false; }

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, psOpaque, psMasked, psTrans;
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
		ci.Desc.Name = "fua scene VS";
		ci.Source = kSceneVS;
		dev->CreateShader(ci, &vs);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua scene PS opaque";
		ci.Source = kScenePSOpaque;
		dev->CreateShader(ci, &psOpaque);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua scene PS masked";
		ci.Source = kScenePS;
		dev->CreateShader(ci, &psMasked);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua scene PS translucent";
		ci.Source = kScenePSTrans;
		dev->CreateShader(ci, &psTrans);
	}
	if (!vs || !psOpaque || !psMasked || !psTrans)
	{ err = "scene shader compilation failed"; return false; }

	Diligent::BufferDesc cbd;
	cbd.Name = "fua scene constants";
	cbd.Size = sizeof(float) * 24;   // mat4 uMVP + vec4 uCameraPos + vec4 uLightParams
	cbd.Usage = Diligent::USAGE_DYNAMIC;
	cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
	cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
	dev->CreateBuffer(cbd, nullptr, &g_cb);
	if (!g_cb) { err = "constant buffer creation failed"; return false; }

	// [rc4l] The dynamic light buffer, created ONCE at a fixed capacity.
	//
	// It is bound to the pipelines as a STATIC variable, which Diligent copies into every SRB at
	// creation -- so recreating the buffer to grow it would silently leave existing SRBs pointing at
	// the freed one. 65536 vec4s is 1 MB and holds ~32000 lights; a Doom frame produces a handful.
	if (!g_lightBuf)
	{
		g_lightBufCapacity = 65536;
		Diligent::BufferDesc lbd;
		lbd.Name = "fua light buffer";
		lbd.Size = (Diligent::Uint64)g_lightBufCapacity * 16;
		lbd.Usage = Diligent::USAGE_DEFAULT;
		lbd.BindFlags = Diligent::BIND_SHADER_RESOURCE;
		lbd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
		lbd.ElementByteStride = 16;
		dev->CreateBuffer(lbd, nullptr, &g_lightBuf);
		if (!g_lightBuf) { err = "light buffer creation failed"; return false; }
	}

	// [rc4l] `false`, not Diligent::False -- something in the reshaped DXSDK headers defines False as
	// a macro, so the qualified name does not survive the preprocessor here.
	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 3, Diligent::VT_FLOAT32, false},   // softLight, fogDensity, alpha
		Diligent::LayoutElement{4, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{5, 0, 1, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{6, 0, 3, Diligent::VT_FLOAT32, false},   // surface normal
	};

	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
	};

	// [rc4l] Four pipelines over the same vertex layout and resources:
	//   0 opaque        no discard, depth write -- early-Z survives
	//   1 masked        alpha test, depth write -- the world and opaque sprites
	//   2 translucent   src-alpha blend, depth test but NO depth write
	//   3 additive      src-alpha / one, likewise
	// Blended geometry must not write depth, or a nearer translucent sprite would occlude the one
	// behind it instead of letting it show through.
	for (int pass = 0; pass < 4; pass++)
	{
		static const char *kNames[4] = { "fua scene PSO opaque", "fua scene PSO masked",
		                                 "fua scene PSO translucent", "fua scene PSO additive" };
		const bool blended = (pass >= 2);

		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = kNames[pass];
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
		pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode =
			(fua_dg_cull == 1) ? Diligent::CULL_MODE_BACK :
			(fua_dg_cull == 2) ? Diligent::CULL_MODE_FRONT : Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = !blended;
		if (blended)
		{
			auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
			rt.BlendEnable = true;
			rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
			rt.DestBlend = (pass == 3) ? Diligent::BLEND_FACTOR_ONE
			                           : Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
			rt.SrcBlendAlpha  = Diligent::BLEND_FACTOR_ONE;
			rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
			rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
		}
		pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
		pci.GraphicsPipeline.InputLayout.NumElements = 7;
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 1;
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
		pci.pVS = vs;
		pci.pPS = (pass == 0) ? psOpaque : (pass == 1) ? psMasked : psTrans;

		Diligent::RefCntAutoPtr<Diligent::IPipelineState> made;
		dev->CreateGraphicsPipelineState(pci, &made);
		if (pass == 0)      g_scenePSO  = made;
		else if (pass == 1) g_maskedPSO = made;
		else if (pass == 2) g_transPSO  = made;
		else                g_addPSO    = made;
	}
	if (!g_scenePSO || !g_maskedPSO || !g_transPSO || !g_addPSO)
	{ err = "scene pipeline creation failed"; return false; }

	// [rc4l] Both stages read Constants now -- the pixel shader needs uCameraPos for radial fog.
	// A stage that declares the block but never gets it bound reads garbage rather than failing.
	Diligent::IPipelineState *psos[] = { g_scenePSO, g_maskedPSO, g_transPSO, g_addPSO };
	const Diligent::SHADER_TYPE stages[] = { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL };
	for (int i = 0; i < 4; i++)
	{
		for (int s = 0; s < 2; s++)
			if (auto *var = psos[i]->GetStaticVariableByName(stages[s], "Constants"))
				var->Set(g_cb);
		// [rc4l] The light buffer is shared by every world pass, so it is STATIC and set once here.
		//
		// Reported rather than silently skipped: a `if (auto *v = GetStaticVariableByName(...))` that
		// does not find its variable binds nothing and the shader reads zeros, which shows up as
		// "dynamic lights do nothing" with no error anywhere. Diligent names an SSBO by its block
		// name, but that is worth confirming rather than assuming.
		auto *lv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightBuffer");
		if (lv) lv->Set(g_lightBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		else g_lightBindFailed = true;
	}

	g_maskedPSO->CreateShaderResourceBinding(&g_srbMasked, true);
	g_scenePSO->CreateShaderResourceBinding(&g_srb, true);
	return true;
}

// [rc4l] Build the backend vertex buffer from the level mesh, and upload it.
//
// Called at upload AND whenever geometry moves. A door does not merely nudge its vertices: the wall
// cache re-bakes the seg, the piece can change vertex count, and MeshStore then hands it a NEW range
// at the top of the arena. Any map from old mesh offsets to buffer slots is stale the moment that
// happens -- which is why patching pieces in place left the shut door painted across an open doorway.
//
// So the whole thing is rebuilt. That is only affordable because the material sort is no longer
// quadratic; it runs for the handful of frames a door is actually moving, and not at all otherwise.
static bool BuildSceneBuffer(FString &err)
{
	int srcCount = 0;
	const FFlatVertex *src = zx::levelmesh::MeshVertexData(srcCount);
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	if (src == NULL || srcCount <= 0 || pieces == NULL || npieces <= 0)
	{
		err = "no baked geometry -- set gl_wallmesh 1, walk the level, then retry";
		return false;
	}

	// [rc4l] Order the pieces by material, then emit their vertices in that order. The result is one
	// contiguous run per material, so each material draws once -- instead of one draw per piece.
	//
	// std::sort, not the selection sort this used to be. At 18000 pieces the quadratic version was
	// ~160 million comparisons -- several seconds, tolerable exactly once at upload and completely
	// prohibitive the moment a door made this have to run again mid-game.
	static TArray<int> order;
	order.Clear();
	order.Resize(npieces);
	for (int i = 0; i < npieces; i++) order[i] = i;
	std::sort(&order[0], &order[0] + npieces, [pieces](int a, int b) {
		return pieces[a].material < pieces[b].material;
	});

	g_sceneVB.Clear();
	g_batches.Clear();
	g_pieceMap.Clear();
	const void *cur = (const void *)(size_t)-1;
	for (int i = 0; i < npieces; i++)
	{
		const zx::levelmesh::MeshPiece &p = pieces[order[i]];
		if (p.range.count == 0) continue;

		if (p.material != cur)
		{
			SceneBatch b;
			b.material = p.material;
			b.first = (unsigned int)g_sceneVB.Size();
			b.count = 0;
			b.masked = MaterialIsMasked(p.material);
			b.srb = NULL;   // filled once the batch list is final
			b.baseTex = p.baseTex;
			b.resolved = p.material;
			g_batches.Push(b);
			cur = p.material;
		}

		// [rc4l] Straight from the mesh: these are the values the engine's own gl_SetColor/gl_SetFog
		// produced for this surface at bake time. The backend re-derived them once and drifted --
		// see CaptureShading in staticmesh.cpp.
		const unsigned int vbStart = g_sceneVB.Size();
		for (unsigned int v = 0; v < p.range.count; v++)
		{
			const FFlatVertex &sv = src[p.range.offset + v];
			SceneVertex dv;
			dv.x = sv.x; dv.y = sv.z; dv.z = sv.y;   // FFlatVertex stores x, z(up), y
			dv.u = sv.u; dv.v = sv.v;
			dv.r = p.colorR; dv.g = p.colorG; dv.b = p.colorB;
			dv.softLight = p.softLight;
			dv.fogDensity = p.fogDensity;
			dv.alpha = p.alpha;
			dv.fogR = ((p.fogColor >> 16) & 0xff) / 255.f;
			dv.fogG = ((p.fogColor >> 8) & 0xff) / 255.f;
			dv.fogB = (p.fogColor & 0xff) / 255.f;
			dv.fogMode = (float)p.fogMode;
			dv.lightIndex = (float)p.dynLightIndex;
			dv.nx = p.normX; dv.ny = p.normY; dv.nz = p.normZ;
			g_sceneVB.Push(dv);
		}
		// [rc4l] Remember where this piece's mesh vertices landed in the backend's own buffer, so a
		// later change to them can be re-uploaded without re-sorting and re-emitting the whole level.
		// The sort is O(n^2) over ~18000 pieces; doing it per frame while a door moves is not an option.
		{
			PieceMap pm;
			pm.meshOffset = p.range.offset;
			pm.count      = p.range.count;
			pm.vbOffset   = vbStart;
			g_pieceMap.Push(pm);
		}
		g_batches[g_batches.Size() - 1].count += p.range.count;
	}

	if (g_sceneVB.Size() == 0) { err = "no drawable pieces"; return false; }

	Diligent::BufferDesc bd;
	bd.Name = "fua scene VB";
	bd.Size = (Diligent::Uint64)g_sceneVB.Size() * sizeof(SceneVertex);
	// [rc4l] USAGE_DEFAULT, not IMMUTABLE. The level mesh is not as static as its name suggests:
	// doors, lifts and crushers move sector planes, the wall cache re-bakes those segs, and the
	// vertices change. An IMMUTABLE buffer cannot be updated at all, so every moving thing in the
	// level was frozen in the backend's view -- a door would open in the GL window and stay shut here.
	bd.Usage = Diligent::USAGE_DEFAULT;
	bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	Diligent::BufferData bdata;
	bdata.pData = &g_sceneVB[0];
	bdata.DataSize = bd.Size;
	g_vb.Release();
	GetDevice()->CreateBuffer(bd, &bdata, &g_vb);
	if (!g_vb) { err = "vertex buffer creation failed"; return false; }

	// [rc4l] Give every batch its own SRB now that the list is final. Sized once, so the RefCntAutoPtrs
	// never move and the raw pointers handed to SceneBatch stay valid.
	ReleaseBatchSRBs();
	for (unsigned i = 0; i < g_batches.Size(); i++)
		g_batches[i].srb = GetMaterialSRB(g_maskedPSO, g_batches[i].material);
	return true;
}
bool SceneUpload(FString &report)
{
	FString err;
	// [rc4l] Size the backend's client area to the engine's screen BEFORE the window exists, so the
	// 2D layer maps 1:1 and a GL/Vulkan screenshot pair is directly comparable. A 624x361 client area
	// against a 640x480 2D layer squashed the HUD to 75% height and point-sampled it into mush.
	if (screen != NULL) Fua_SetBackendWindowSize(screen->GetWidth(), screen->GetHeight());
	if (!DiligentShowWindow(err)) { report = err; return false; }
	if (!EnsureScenePipeline(err)) { report = err; return false; }
	if (!BuildSceneBuffer(err)) { report = err; return false; }

	g_sceneVerts = (int)g_sceneVB.Size();
	BuildMVP(g_mvp);

	// [rc4l] The shading inputs themselves, not just which code path they take.
	//
	// The Vulkan render came out uniformly brighter and less tinted than GL's, and three rounds of
	// reading pixels off screenshots produced three different theories. The values are right here at
	// bake time; printing them settles in one run what guessing did not settle in an hour.
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	{
		int lmin = 999, lmax = -1;
		double lsum = 0;
		unsigned int lc = 0, fc = 0; int lcN = 0, fcN = 0;
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[i];
			if (p.range.count == 0) continue;
			if (p.lightLevel < lmin) lmin = p.lightLevel;
			if (p.lightLevel > lmax) lmax = p.lightLevel;
			lsum += p.lightLevel;
			if (lcN == 0 || p.lightColor != lc) { lc = p.lightColor; lcN++; }
			if (fcN == 0 || p.fadeColor != fc) { fc = p.fadeColor; fcN++; }
		}
		Printf("  mesh light: level %d..%d (mean %.1f), lightColor %08x, fadeColor %08x, "
			"fogdensity(first) %.2f\n",
			lmin, lmax, npieces ? lsum / npieces : 0.0, lc, fc,
			gl_GetFogDensity(lmin, PalEntry(fc)));
	}

	// [rc4l] Which lighting path is live decides how the picture should be read: lightmode 8 takes
	// R_DoomLightingEquation, everything else takes fog diminishing. Printing it beats inferring it
	// from a screenshot.
	int softPieces = 0, fogPieces = 0;
	for (unsigned int i = 0; i < g_sceneVB.Size(); i++)
	{
		if (g_sceneVB[i].softLight >= 0.f) softPieces++;
		if (g_sceneVB[i].fogMode != 0.f) fogPieces++;
	}
	report.Format("uploaded %d verts (%.2f MB), %d pieces -> %d material batches "
		"[lightmode %d, fogmode %d, %d%% soft-lit, %d%% fogged]",
		g_sceneVerts, (double)g_sceneVB.Size() * sizeof(SceneVertex) / (1024.0 * 1024.0),
		npieces, (int)g_batches.Size(),
		glset.lightmode, (int)gl_fogmode,
		softPieces * 100 / g_sceneVerts, fogPieces * 100 / g_sceneVerts);
	return true;
}

// [rc4l] Re-upload the parts of the world that actually moved.
//
// Doors, lifts, crushers and rising floors change sector planes; the wall cache notices, re-bakes
// those segs, and MeshStore rewrites their vertices in place. Only the pieces overlapping the mesh's
// dirty range are rebuilt and uploaded -- a door costs a handful of pieces, not the 8.9 MB the whole
// level would.
//
// Positions and UVs are refreshed but the batch layout is not: a piece keeps its slot in the
// material-sorted buffer, so nothing needs re-sorting. A surface that changes its *material* mid-game
// would need a full re-upload; nothing in Doom does that outside of animation, which is handled
// separately by swapping the SRB.
static void RefreshMovedGeometry(Diligent::IDeviceContext *ctx)
{
	if (!g_vb || g_pieceMap.Size() == 0) return;

	unsigned int lo = 0, hi = 0;
	zx::levelmesh::MeshTakeDirty(lo, hi);
	if (hi <= lo) { g_geomUpdates = 0; return; }
	g_lastDirtyLo = lo; g_lastDirtyHi = hi;

	int srcCount = 0;
	const FFlatVertex *src = zx::levelmesh::MeshVertexData(srcCount);
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	if (src == NULL || pieces == NULL) return;

	// [rc4l] Rebuild everything rather than patch the touched pieces.
	//
	// Patching in place was tried and is wrong: a re-baked piece can change vertex count, and
	// MeshStore then gives it a NEW range at the top of the arena. Every offset recorded at upload
	// time is stale from that moment, so the patch copied old vertices into the right slot and the
	// new geometry had no slot at all -- the shut door stayed painted across an open doorway while
	// GL showed the room behind it. The dirty range GROWING (5319 -> 5328) was the visible symptom.
	//
	// A full rebuild costs a sort and a buffer upload, and only happens on the frames something
	// actually moves.
	FString err;
	g_geomUpdates = BuildSceneBuffer(err) ? 1 : 0;
	g_sceneVerts = (int)g_sceneVB.Size();
	g_geomRebuilds += g_geomUpdates;
	(void)src; (void)pieces; (void)npieces;
}

// [rc4l] Collect every active dynamic light in the level into the shared storage buffer.
//
// Rebuilt every frame -- lights move, spawn and die. See fuaDynLight in the shader for why there is
// no per-surface light list at all.
static void CollectDynamicLights(Diligent::IDeviceContext *ctx)
{
	g_lightCount = 0;
	if (fua_dg_dynlights && g_lightBuf)
	{
		// [rc4l] Straight from the level's thinkers, not from the engine's per-surface light buffer.
		// Same filters gl_GetLight applies: dormant lights are skipped, and a zero radius means off.
		static TArray<float> lightData;
		lightData.Clear();
		TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
		ADynamicLight *light;
		while ((light = it.Next()) != NULL)
		{
			if (!light->IsActive()) continue;
			const float radius = light->GetRadius() * gl_lights_size;
			if (radius <= 0.f) continue;
			if ((unsigned)g_lightCount >= g_lightBufCapacity / 2) break;

			// The additive/subtractive intensity scaling gl_GetLight applies.
			const float cs = (gl_lights_additive || (light->flags4 & MF4_ADDITIVE)) ? 0.2f : 1.0f;
			float r = light->GetRed()   / 255.0f * cs * gl_lights_intensity;
			float g = light->GetGreen() / 255.0f * cs * gl_lights_intensity;
			float b = light->GetBlue()  / 255.0f * cs * gl_lights_intensity;

			// Same axis swap the mesh uses: (x, z, y).
			lightData.Push(FIXED2FLOAT(light->x));
			lightData.Push(FIXED2FLOAT(light->z));
			lightData.Push(FIXED2FLOAT(light->y));
			lightData.Push(radius);
			lightData.Push(r);
			lightData.Push(g);
			lightData.Push(b);
			lightData.Push(light->IsSubtractive() ? 1.0f : 0.0f);
			g_lightCount++;
		}
		if (g_lightCount > 0)
			ctx->UpdateBuffer(g_lightBuf, 0, (Diligent::Uint64)lightData.Size() * 4, &lightData[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
}

static void DrawSceneOnce(bool present = true, bool pump = true)
{
	auto *ctx = GetContext();
	auto *swap = GetSwapChain();

	auto *rtv = swap->GetCurrentBackBufferRTV();
	auto *dsv = swap->GetDepthBufferDSV();
	ctx->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// [rc4l] Lights are collected BEFORE the constants are written -- the shader reads the count from
	// there, so collecting afterwards would light every frame with the previous frame's count.
	RefreshMovedGeometry(ctx);
	CollectDynamicLights(ctx);

	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int i = 0; i < 16; i++) cb[i] = g_mvp[i];
		// [rc4l] Same axis swap the vertices get: the mesh is (x, z-up, y).
		cb[16] = FIXED2FLOAT(viewx); cb[17] = FIXED2FLOAT(viewz);
		cb[18] = FIXED2FLOAT(viewy); cb[19] = (float)(int)fua_dg_lightmode;
		cb[20] = (float)g_lightCount; cb[21] = 0.f; cb[22] = 0.f; cb[23] = 0.f;
	}

	// [rc4l] Re-resolve animated textures.
	//
	// A batch's material was resolved once at bake time, so without this every animated surface in
	// the level freezes on whichever frame was showing: nukage stops flowing, computer screens stop
	// flickering, and the world looks subtly dead. TexMan() applies the animation translation, so
	// this is a table lookup per batch (~55 on Sunder MAP10) and only touches the SRB when the frame
	// actually changed.
	if (fua_dg_animate)
	{
		for (unsigned i = 0; i < g_batches.Size(); i++)
		{
			SceneBatch &b = g_batches[i];
			if (b.baseTex == NULL) continue;
			FMaterial *now = FMaterial::ValidateTexture(((FTexture *)b.baseTex)->id, false, true);
			if (now == NULL || now == b.resolved) continue;
			b.resolved = now;
			g_animSwaps++;
			if (auto *srb = GetMaterialSRB(g_maskedPSO, now)) b.srb = srb;
		}
	}

	// [rc4l] Sky first, with depth off, so the world paints over whatever it does not cover.
	if (fua_dg_sky) { EnsureSky(); DrawSky(ctx); }

	Diligent::IBuffer *vbs[] = { g_vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_scenePSO);

	// [rc4l] One alpha-tested pipeline for everything.
	//
	// An opaque/masked split was tried and REVERTED: it neither helped (presented stayed 1.77 ms,
	// because that time is swapchain present, not fragment shading) nor rendered correctly -- routing
	// materials FMaterial::isMasked() calls opaque made their transparent texels paint over detail
	// behind them. The lesson is that isMasked() answers a GL-pipeline question, not "does this
	// texture have see-through pixels", so it cannot drive a pass split on its own.
	ctx->SetPipelineState(g_maskedPSO);

	// [rc4l] g_drawRepeat re-draws the whole scene N times per frame.
	//
	// This is the scale probe. Measured GPU cost of the visible set is ~0.013 ms/frame, which is not a
	// number you can plan against -- it is small enough to be noise, and "the GPU is idle" is a claim,
	// not a measurement. Redrawing the same geometry N times is a cheap way to find where the GPU
	// actually starts to cost something, and that threshold is what decides whether the BSP walk and
	// the clipper can be deleted in favour of just drawing the level.
	for (int rep = 0; rep < g_drawRepeat; rep++)
	for (unsigned bi = 0; bi < g_batches.Size(); bi++)
	{
		const SceneBatch &b = g_batches[bi];
		if (b.count == 0 || b.srb == NULL) continue;

		ctx->CommitShaderResources(b.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		Diligent::DrawAttribs draw;
		draw.NumVertices = b.count;
		draw.StartVertexLocation = b.first;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
	}

	// [rc4l] Sprites last: the world is opaque and fills the depth buffer first, so alpha-tested
	// sprite quads reject most of their fragments instead of shading them.
	DrawDynamic(ctx);

	// [rc4l] 2D last, over everything, with depth off entirely -- it is the frame's top layer.
	Draw2D(ctx);

	if (present)
	{
		swap->Present(0);
		// [rc4l] The Win32 message pump is NOT free -- PeekMessage on a window with pending paint
		// work costs more than the draw at these scales. Pumping every frame put 1.5 ms into a
		// 0.01 ms submission, so the benchmark pumps sparsely and a real backend would pump once
		// per engine frame, not once per present.
		if (pump) Fua_PumpBackendWindow(GetBackendWindow());
		// The embedded surface follows the engine's client area, which can change at runtime.
		Fua_SyncBackendWindowToParent(GetBackendWindow());
	}
}

bool SceneBench(int frames, FString &report)
{
	if (!g_vb || !g_scenePSO) { report = "call fua_diligent_scene first"; return false; }
	if (frames < 1) frames = 1;

	for (int i = 0; i < 20; i++) DrawSceneOnce(true, true);   // warmup
	GetContext()->WaitForIdle();

	// Presented frames -- what the backend actually costs end to end. Pump every 60th frame so the
	// window stays responsive without the pump dominating the measurement.
	const DWORD p0 = I_MSTime();
	for (int i = 0; i < frames; i++) DrawSceneOnce(true, (i % 60) == 0);
	GetContext()->WaitForIdle();
	const DWORD p1 = I_MSTime();

	// Submission only -- the apples-to-apples half against fua_gl_meshbench.
	for (int i = 0; i < 20; i++) DrawSceneOnce(false, false);
	GetContext()->WaitForIdle();
	const DWORD s0 = I_MSTime();
	for (int i = 0; i < frames; i++) DrawSceneOnce(false, false);
	GetContext()->WaitForIdle();
	const DWORD s1 = I_MSTime();

	const double ptotal = (double)(p1 - p0);
	const double stotal = (double)(s1 - s0);

	// [rc4l] Real GPU time, from a timestamp query.
	//
	// The presented figure moved 1.68 -> 0.27 ms/frame between two runs of IDENTICAL geometry, while
	// submit-only did not budge. That swing is the compositor: whether the backend window happens to
	// be occluded decides how long Present blocks, and it has nothing to do with the renderer. A
	// number that answers "is this backend fast" cannot be a number the window manager gets a vote
	// in, so ask the GPU how long it actually spent.
	double gpuMs = -1.0;
	if (GetDevice()->GetDeviceInfo().Features.TimestampQueries)
	{
		Diligent::QueryDesc qd;
		qd.Name = "fua scene duration";
		qd.Type = Diligent::QUERY_TYPE_DURATION;
		Diligent::RefCntAutoPtr<Diligent::IQuery> q;
		GetDevice()->CreateQuery(qd, &q);
		if (q)
		{
			const int gframes = frames < 64 ? frames : 64;
			auto *ctx = GetContext();
			ctx->BeginQuery(q);
			for (int i = 0; i < gframes; i++) DrawSceneOnce(false, false);
			ctx->EndQuery(q);
			ctx->WaitForIdle();
			Diligent::QueryDataDuration d;
			if (q->GetData(&d, sizeof(d)) && d.Frequency != 0)
				gpuMs = 1000.0 * (double)d.Duration / (double)d.Frequency / gframes;
		}
	}

	FString gpu;
	if (gpuMs >= 0.0) gpu.Format(", GPU %.4f ms/frame", gpuMs);
	else gpu = ", GPU n/a (no timestamp queries)";

	report.Format("Diligent: %d frames, %d verts (%d tris), %d batches, %d textures -- "
		"submit-only %.4f ms/frame%s; presented %.4f ms/frame (%.0f fps, includes compositor wait)",
		frames, g_sceneVerts, g_sceneVerts / 3, (int)g_batches.Size(), MaterialCount(),
		stotal / frames, gpu.GetChars(),
		ptotal / frames, 1000.0 * frames / (ptotal > 0 ? ptotal : 1));
	return true;
}

// [rc4l] One live frame from the current camera. See dglive.h for why the engine-side declaration
// lives in its own header.
// [rc4l] Bake and upload this level, once, without anyone typing anything.
//
// The bake does not happen when it is asked for -- ArmFullBake only sets a flag that the next
// rendered frame acts on -- so the upload cannot follow in the same breath, and an upload with no
// mesh behind it fails with "no baked geometry" and would leave the backend permanently blank. Hence
// the wait: arm, let a frame or two go by, then upload.
static bool AutoSetupForLevel()
{
	static int  s_gen = -1;
	static int  s_state = 0;   // 0 arm, 1 waiting for the bake, 2 ready, 3 gave up
	static int  s_wait = 0;
	static int  s_tries = 0;

	const int gen = zx::levelmesh::LevelGeneration();
	if (gen != s_gen) { s_gen = gen; s_state = 0; s_wait = 0; s_tries = 0; }

	switch (s_state)
	{
	case 0:
		if (gamestate != GS_LEVEL) return false;
		// [rc4l] The switch implies the level mesh. gl_wallmesh is off by default, so the first
		// self-arming run baked nothing and reported "no baked geometry -- set gl_wallmesh 1, walk
		// the level, then retry" -- advice aimed at a person typing commands, from a path whose whole
		// point is that nobody is.
		if (!gl_wallmesh) gl_wallmesh = true;
		zx::levelmesh::ArmFullBake();
		s_state = 1; s_wait = 0;
		return false;
	case 1:
		// The bake runs on a rendered frame, and a big level needs more than one. Retry rather than
		// give up on the first miss: giving up is permanent for the level, and the cost of being
		// wrong is a black screen with no way back short of a restart.
		if (++s_wait < 4) return false;
		{
			FString report;
			if (SceneUpload(report))
			{
				s_state = 2;
				Printf("vulkan: %s\n", report.GetChars());
			}
			else if (++s_tries < 8)
			{
				s_state = 0;   // re-arm and try again
			}
			else
			{
				s_state = 3;
				Printf("vulkan: setup failed after %d attempts -- %s\n", s_tries, report.GetChars());
			}
		}
		return s_state == 2;
	case 2:
		return true;
	default:
		return false;
	}
}

void LiveFrame()
{
	// [rc4l] fua_vulkan is the user-facing switch and sets itself up; fua_diligent_live is the manual
	// override that assumes someone already ran the bake by hand.
	const bool autoReady = fua_vulkan ? AutoSetupForLevel() : false;

	// Uncover the GL frame underneath when the backend is off, rather than leaving the last Vulkan
	// frame frozen over it. This is what makes turning it off an A/B toggle you can hold.
	if (!autoReady && !fua_diligent_live)
	{
		Fua_ShowBackendWindow(GetBackendWindow(), 0);
		return;
	}
	Fua_ShowBackendWindow(GetBackendWindow(), 1);
	if (!g_vb || !g_scenePSO || g_sceneVerts == 0) return;
	if (GetBackendWindow() == NULL) return;

	// The camera is rebuilt every frame -- that is the entire point of this path. Everything else
	// (geometry, materials, per-vertex light) is already resident, so a live frame is a matrix
	// upload and the batch loop.
	BuildMVP(g_mvp);
	DrawSceneOnce(true, true);
}

// [rc4l] What the last dynamic pass actually drew, split by blend mode.
void DynStats(FString &report)
{
	int sprites = zx::levelmesh::SpritePieceCount();
	report.Format("dynamic: %d sprites -> %d draws, %d tris "
		"[opaque %d, translucent %d, additive %d, fuzz %d]",
		sprites, g_dynDraws, g_dynTris,
		g_dynByBlend[0], g_dynByBlend[1], g_dynByBlend[2], g_dynByBlend[3]);
	FString anim;
	anim.Format(" | %d animation frame swaps since load | light buffer %d vec4s (~%d lights)",
		g_animSwaps, g_lightCount * 2, g_lightCount);
	FString geo;
	geo.Format(" | geometry: %d scene rebuilds since load, last dirty %u..%u",
		g_geomRebuilds, g_lastDirtyLo, g_lastDirtyHi);
	report += geo;
	if (g_lightBindFailed) report += " | WARNING: LightBuffer not bound";
	report += anim;
}

// [rc4l] Measure GPU time for the current scene at a given draw multiplier.
static double MeasureGpuMs(int frames)
{
	if (!GetDevice()->GetDeviceInfo().Features.TimestampQueries) return -1.0;
	Diligent::QueryDesc qd;
	qd.Name = "fua scale duration";
	qd.Type = Diligent::QUERY_TYPE_DURATION;
	Diligent::RefCntAutoPtr<Diligent::IQuery> q;
	GetDevice()->CreateQuery(qd, &q);
	if (!q) return -1.0;

	auto *ctx = GetContext();
	for (int i = 0; i < 5; i++) DrawSceneOnce(false, false);   // warmup at this multiplier
	ctx->WaitForIdle();

	ctx->BeginQuery(q);
	for (int i = 0; i < frames; i++) DrawSceneOnce(false, false);
	ctx->EndQuery(q);
	ctx->WaitForIdle();

	Diligent::QueryDataDuration d;
	if (!q->GetData(&d, sizeof(d)) || d.Frequency == 0) return -1.0;
	return 1000.0 * (double)d.Duration / (double)d.Frequency / frames;
}

// [rc4l] The scale probe: how much geometry can this GPU draw before it costs a frame's worth of
// time? The whole argument for deleting the BSP walk rests on the answer. If the visible set costs
// 0.013 ms and 50x the visible set still costs well under a millisecond, then culling on the CPU is
// buying nothing that drawing everything would not give for free.
//
// Reported as ms and as an implied budget in triangles, because "how many triangles fit in 1 ms" is
// the form the answer is actually needed in.
bool SceneScale(int frames, FString &report)
{
	if (!g_vb || !g_scenePSO) { report = "call fua_diligent_scene first"; return false; }
	if (frames < 1) frames = 60;

	static const int kSteps[] = { 1, 2, 5, 10, 25, 50, 100 };
	const int tris = g_sceneVerts / 3;
	FString out;
	out.Format("Diligent scale probe: %d tris/copy, %d batches\n", tris, (int)g_batches.Size());

	const int saved = g_drawRepeat;
	double perTriMs = -1.0;
	for (unsigned s = 0; s < sizeof(kSteps) / sizeof(kSteps[0]); s++)
	{
		g_drawRepeat = kSteps[s];
		const double ms = MeasureGpuMs(frames);
		if (ms < 0.0) { out += "  timestamp queries unavailable\n"; break; }
		FString line;
		line.Format("  %3dx = %8d tris, %4d draws -> GPU %7.4f ms\n",
			kSteps[s], tris * kSteps[s], (int)g_batches.Size() * kSteps[s], ms);
		out += line;
		perTriMs = ms / (double)(tris * kSteps[s]);
	}
	g_drawRepeat = saved;

	if (perTriMs > 0.0)
	{
		FString line;
		line.Format("  => ~%.0f tris per GPU millisecond at this resolution and shader",
			1.0 / perTriMs);
		out += line;
	}
	report = out;
	return true;
}

// [rc4l] Read the swapchain back and write a PNG from inside the engine.
//
// Screen-grabbing the backend window is not a verification: it captures whatever is in front of it,
// which on a busy desktop is another window entirely. Reading the actual backbuffer is the only way
// to prove the render is correct, and it is also how a CI check would do it.
bool SceneScreenshot(const char *path, FString &report)
{
	if (!g_vb || !g_scenePSO) { report = "call fua_diligent_scene first"; return false; }

	auto *ctx = GetContext();
	auto *swap = GetSwapChain();
	auto *dev = GetDevice();

	// [rc4l] Always from the CURRENT camera. The MVP used to be whatever `fua_diligent_scene` had
	// snapshotted, so a screenshot taken after the player turned showed the room they used to be
	// looking at -- and every GL/Vulkan comparison made that way was of two different viewpoints.
	// That cost a long detour: MAP01 looked like it was missing half its geometry when it was simply
	// facing the other way.
	BuildMVP(g_mvp);

	// [rc4l] Draw WITHOUT presenting. Present flips to the next image in the swapchain, so reading
	// GetCurrentBackBufferRTV afterwards returns a buffer whose contents are undefined -- which read
	// back as a perfectly convincing all-black PNG the first time.
	DrawSceneOnce(false);
	ctx->WaitForIdle();

	auto *backTex = swap->GetCurrentBackBufferRTV()->GetTexture();
	const auto &bd = backTex->GetDesc();

	Diligent::TextureDesc sd;
	sd.Name = "fua readback";
	sd.Type = Diligent::RESOURCE_DIM_TEX_2D;
	sd.Width = bd.Width; sd.Height = bd.Height;
	sd.Format = bd.Format;
	sd.Usage = Diligent::USAGE_STAGING;
	sd.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
	sd.BindFlags = Diligent::BIND_NONE;
	sd.MipLevels = 1;
	Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
	dev->CreateTexture(sd, nullptr, &staging);
	if (!staging) { report = "staging texture creation failed"; return false; }

	Diligent::CopyTextureAttribs cta;
	cta.pSrcTexture = backTex;
	cta.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	cta.pDstTexture = staging;
	cta.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->CopyTexture(cta);
	ctx->WaitForIdle();

	Diligent::MappedTextureSubresource mapped;
	ctx->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
	if (mapped.pData == nullptr) { report = "readback map failed"; return false; }

	const int w = (int)bd.Width, h = (int)bd.Height;

	// [rc4l] Channel order comes from the FORMAT, not from an assumption.
	//
	// This used to hard-code a BGRA->RGB swap. When the swapchain came back RGBA that swap *created*
	// a red/blue inversion in every screenshot, and since Doom textures are muddy browns and greys
	// the result still looked like a plausible Doom scene -- just "cooler". It cost a long detour
	// hunting a lighting bug that did not exist.
	const bool bgra = (bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM ||
	                   bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB);
	const int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;

	BYTE *rgb = new BYTE[w * h * 3];
	for (int y = 0; y < h; y++)
	{
		const BYTE *srcRow = (const BYTE *)mapped.pData + (size_t)y * mapped.Stride;
		BYTE *dstRow = rgb + (size_t)y * w * 3;
		for (int x = 0; x < w; x++)
		{
			dstRow[x*3+0] = srcRow[x*4+ri];
			dstRow[x*3+1] = srcRow[x*4+1];
			dstRow[x*3+2] = srcRow[x*4+bi];
		}
	}
	ctx->UnmapTextureSubresource(staging, 0, 0);

	FILE *f = fopen(path, "wb");
	bool ok = false;
	if (f)
	{
		ok = M_CreatePNG(f, rgb, NULL, SS_RGB, w, h, w * 3) && M_FinishPNG(f);
		fclose(f);
	}
	delete[] rgb;

	if (!ok) { report.Format("failed to write %s", path); return false; }
	// [rc4l] The camera is reported because a GL/Vulkan screenshot pair is only meaningful if both
	// used the same one, and "are these two images of the same viewpoint" turned out to be a
	// question worth answering with numbers rather than by eye -- twice.
	report.Format("wrote %s (%dx%d, swapchain format %d, %s) cam=(%.1f, %.1f, %.1f) yaw=%.1f",
		path, w, h, (int)bd.Format, bgra ? "BGRA" : "RGBA",
		FIXED2FLOAT(viewx), FIXED2FLOAT(viewy), FIXED2FLOAT(viewz),
		(float)(viewangle >> ANGLETOFINESHIFT) * 360.0f / 8192.0f);
	return true;
}

}} // namespace zx::hwrender

CCMD( fua_diligent_scene )
{
	FString report;
	const bool ok = zx::hwrender::SceneUpload( report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_diligent_shot )
{
	const char *path = ( argv.argc( ) > 1 ) ? argv[1] : "fua_vk.png";
	FString report;
	const bool ok = zx::hwrender::SceneScreenshot( path, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_diligent_bench )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 300;
	FString report;
	const bool ok = zx::hwrender::SceneBench( n, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_dg_dynstats )
{
	FString report;
	zx::hwrender::DynStats( report );
	Printf( "%s\n", report.GetChars( ) );
}

CCMD( fua_diligent_scale )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 60;
	FString report;
	const bool ok = zx::hwrender::SceneScale( n, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

#else // !FUA_DILIGENT

CCMD( fua_diligent_scene ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_bench ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_scale ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }

namespace zx { namespace hwrender { void LiveFrame() {} }}

#endif // FUA_DILIGENT
