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
#include "BottomLevelAS.h"
#include "TopLevelAS.h"

#include "features/levelmesh/staticmesh.h"
#include "features/levelmesh/wallcache.h"   // LevelGeneration, for the per-level auto setup
#include "features/levelmesh/levelmesh.h"   // ArmFullBake
#include "d_main.h"                          // gamestate
#include "features/levelmesh/flatmesh.h"
#include "features/levelmesh/projdecals.h"   // [rc4l] the marks the decal pass draws
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
#ifdef ZX_ENABLE_REPLAY
#include "features/replay/zx_replay.h"   // [rc4l] the Vulkan instant-replay stream
#endif

// [rc4l] Declared outside the namespace: EXTERN_CVAR builds a name from the identifier, so a
// namespace-qualified one resolves to a symbol the engine never defines and fails only at link time.
EXTERN_CVAR(Int, gl_fogmode)
// [rc4l] fua_vulkan turns this on itself -- see AutoSetupForLevel.
EXTERN_CVAR(Bool, gl_wallmesh)
// [rc4l] The engine's texture filter mode; the backend mirrors it. See FillSamplerFromEngine.
EXTERN_CVAR(Int, gl_texture_filter)
// [rc4l] The sky's own vertical nudge, both the per-texture one and the global testing cvar.
EXTERN_CVAR(Float, skyoffset)

namespace zx { namespace hwrender { void ReleaseMaterials(); void GetSkyFog(float&,float&,float&,float&); }}
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
// [rc4l] Embed the backend in the engine's window (the default), or give it one of its own beside it.
//
// Embedded is how you PLAY in Vulkan: input goes to the parent as normal and the only pixels are the
// backend's. A separate window is how you SHOW someone a difference -- both renderers on screen at
// once, same camera, no toggling back and forth and trying to remember what the other one looked
// like. Read once, when the window is created.
CVAR(Bool, fua_dg_embed, true, CVAR_ARCHIVE)

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
// [rc4l] Backface culling for the WORLD, on by default.
//
// The level mesh holds both sides of every two-sided line, because it is a cache and the BSP shows
// it each side eventually. Coplanar quads with different vertices do not agree on depth to the last
// bit, so wherever two such walls overlap the rasteriser stipples between them -- 1799 coplanar
// overlapping pairs on dbab02, and a checkerboard seam down a rock face that GL never shows. Each
// side's wall faces into its own sector, so culling back faces drops exactly the one that should not
// be visible. 0 none, 1 back, 2 front (front is a diagnostic: it should show the level inside out).
CVAR(Int, fua_dg_cull, 1, CVAR_ARCHIVE)

// [rc4l] Draw the sky. Off is a diagnostic: the sky fills every pixel the world does not cover, so
// with it on, "missing world geometry" and "correctly visible sky" look identical. Turning it off
// leaves holes against the clear colour, which is unambiguous.
CVAR(Bool, fua_dg_sky, true, 0)
// [rc4l] Reflections are ALWAYS on; this only picks how they are produced.
//
//   on  -- ray traced: one ray per mirror pixel against the level's acceleration structure
//   off -- planar: the world re-rendered from the reflected camera into a screen-sized target
//
// There is no switch for mirrors themselves. A mirror with no reflection is a hole in the wall, not
// a cheaper mirror, so "off" was never a state worth being able to reach. It also falls back on its
// own: an adapter without ray tracing gets the planar path whatever this says.
CVAR(Bool, fua_dg_rtmirrors, false, CVAR_ARCHIVE)

// [rc4l] Re-resolve animated textures per frame. See the loop in DrawSceneOnce.
CVAR(Bool, fua_dg_animate, true, 0)

// [rc4l] Draw the captured 2D layer (HUD, menus, console) over the world.
CVAR(Bool, fua_dg_hud, true, 0)

// [rc4l] Dynamic lights: muzzle flashes, plasma, rocket trails, lamps.
CVAR(Bool, fua_dg_dynlights, true, 0)

// [rc4l] The camera pitch, which lives outside any header the backend already pulls in.
extern int viewpitch;

// [rc4l] Which of the three ways a mark is drawn -- see projdecals.h. Declared at file scope, not
// inside zx::hwrender: a cvar lives in the global namespace, and an EXTERN_CVAR inside a namespace
// declares a DIFFERENT symbol that links against nothing.
EXTERN_CVAR(Int, fua_decalmode)

namespace zx { namespace hwrender {

Diligent::IRenderDevice  *GetDevice();
Diligent::ITextureView   *GetMaterialSRV(const void *materialPtr, int translation);
Diligent::ITextureView   *GetBrightmapSRV(const void *materialPtr);
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
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_maskedGBufPSO; // ...and writing normals
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_transPSO;      // normal translucency
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_addPSO;        // additive
// [rc4l] The same four with culling forced off, for SPRITES.
//
// Backface culling is right for the world and wrong for billboards: a sprite quad is built facing
// the camera and nothing guarantees its winding survives every view angle, so a world-wide cull mode
// would silently eat some of them. The world's setting must not be able to reach them, so they get
// their own pipelines rather than a promise that the winding works out.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_maskedNoCullPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_transNoCullPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_addNoCullPSO;
// [rc4l] And again with a depth bias, for DECALS.
//
// A decal is a quad glued flat against the wall it marks, exactly coplanar with it, so it z-fights
// without help -- GL wraps its decal pass in glPolygonOffset(-1, -128) for the same reason. The bias
// belongs in the pipeline rather than in the geometry: nudging the vertices towards the camera would
// have to know where the camera is, which is exactly what a baked or streamed vertex must not.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_maskedDecalPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_transDecalPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_addDecalPSO;
// [rc4l] And the alpha-mask variants of the same, for shaded decals. See kScenePSRedAlpha.
// [rc4l] The scene's depth, as a texture the shaders can READ.
//
// The swapchain's own depth buffer is write-only from a shader's point of view, so anything that
// wants to know how far away the world is -- projected decals first, and any screen-space effect
// after them -- needs its own. The world pass renders into this instead, and it is the same format
// and size as the one it replaces, so nothing else has to change.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_sceneDepth;
// [rc4l] The G-buffer: the exact surface normal of whatever the world pass drew, for the decals.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_sceneNormal;
static Diligent::ITextureView *SceneNormalRTV();
Diligent::ITextureView *SceneNormalSRV();
// True while the world is being drawn INTO the G-buffer. The camera-texture and mirror paths draw
// the same geometry with only a colour target bound, and a pipeline that writes two attachments
// cannot be used against one.
static bool g_gbufBound = false;
static Diligent::ITextureView *EnsureSceneDepth();
Diligent::ITextureView *SceneDepthSRV();
// The same texture as a depth target, for putting the attachment back after a pass that samples it.
Diligent::ITextureView *SceneDepthDSV();
static int g_sceneDepthW = 0, g_sceneDepthH = 0;

static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_transRedAlphaPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_addRedAlphaPSO;
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
	// [rc4l] 0 opaque/alpha-tested, 1 translucent, 2 additive -- the same numbering the dynamic path
	// uses. A translucent batch is never merged with its neighbour and is drawn in its own pass.
	int          blend;
	// Centroid and surface normal, for sorting the translucent pass back to front each frame and for
	// dropping the face pointing away from the camera. Only meaningful when blend != 0; the opaque
	// pass is sorted by material and never consults these.
	float        sortX, sortY, sortZ;
	float        normX, normY, normZ;   // mesh space: (x, z-up, y)
};
static TArray<SceneBatch> g_batches;
// [rc4l] Indices of the translucent batches, in build order. Small -- a level has a handful of
// translucent surfaces, not thousands -- so re-sorting it per frame costs nothing.
static TArray<int> g_blendBatches;

// [rc4l] One dynamic draw: a contiguous span of this frame's sprite stream sharing a material, a
// blend mode and a translation. File scope because the translucent ones are not drawn by the sprite
// pass at all -- they are merged into the world's translucent pass and sorted against it.
struct DynRun
{
	const void  *material;
	unsigned int first, count;
	int          blend;
	int          translation;
	bool         depthBias;    // decals: coplanar with their wall, so they need the biased pipeline
	bool         redAlpha;     // the texture is an alpha mask, not a colour image
	float        cx, cy, cz;   // centroid, for the merged sort
};
static TArray<DynRun> g_dynRuns;
static bool g_dynReady = false;

// [rc4l] Where each mesh piece landed in the backend's material-sorted vertex buffer, so geometry
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

Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso,
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
		// [rc4l] Every material gets a brightmap, black when it has none, so the shader needs no
		// flag and no second pipeline permutation. See GetBrightmapSRV.
		//
		// Reported once rather than assumed: an unbound sampler reads UNDEFINED, which in practice is
		// whatever texture was bound last, and adding that to the fragment saturates every bright
		// surface to white. "The variable was not found" and "the black texture failed to create"
		// both produce exactly that, and neither says anything on its own.
		{
			auto *v = e.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uBrightmap");
			Diligent::ITextureView *bmv = GetBrightmapSRV(material);
			static bool reported = false;
			if (!reported)
			{
				reported = true;
				Printf("Diligent brightmap: variable %s, black texture %s\n",
					v ? "FOUND" : "MISSING (sampler will be unbound)",
					bmv ? "ok" : "FAILED TO CREATE");
			}
			if (v && bmv) v->Set(bmv);
		}
	}
	g_matSRBs.Push(e);
	return e.srb;
}

// [rc4l] Freeing these means forgetting every RAW copy of them as well.
//
// Each scene batch caches the pointer its material resolved to, once, when the batch list is built
// -- so the batches outlive this call and go on pointing at bindings that have been released.
// Committing one is a use-after-free, and it is reached by resizing or MINIMISING the window: that
// resizes the swapchain, which recreates the scene depth and normal textures, which comes through
// here. The engine died in CommitShaderResources every time, from inside the ordinary opaque loop,
// which reads as a driver fault rather than as our own dangling pointer.
//
// Nulling them is enough because the draw loop re-resolves a batch whose binding has gone.
static void ForgetBatchSRBs();

static void ReleaseMaterialSRBs()
{
	ForgetBatchSRBs();
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
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec3 vColor;\n"
	"layout(location = 2) out vec3 vLightParm;\n"
	"layout(location = 3) out vec4 vFog;\n"
	"layout(location = 4) out vec4 vPixelPos;\n"
	"layout(location = 5) flat out int vLightIndex;\n"
	"layout(location = 6) out vec3 vNormal;\n"
	"layout(location = 7) flat out vec4 vPlane;\n"
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
	// [rc4l] The surface PLANE, flat: its normal and its distance from the origin.
	//
	// Whether a light reaches a surface is a property of the SURFACE, not of the fragment -- one
	// answer for the whole face. Asking it per fragment means asking it of an interpolated
	// position, and the rasteriser returns a world height on a flat floor that wobbles in the last
	// thousandth of a unit. That is harmless until a light sits exactly ON the plane it lights, and
	// then the wobble is the entire answer: the test flips from fragment to fragment and the floor
	// comes out in hard horizontal stripes of lit and unlit. Freedoom MAP01 has such a light at the
	// map start -- a green one at z 8.0 on a floor at 8.0 -- which is how this was found.
	//
	// Interpolating nothing removes the wobble rather than hiding it under a tolerance: every
	// vertex of a planar face gives the same dot(n, p), so the flat value is that number exactly and
	// the test is identical across the face however far away or however oblique it is.
	"    vPlane = vec4(aNormal, dot(aNormal, aPos));\n"
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
	"layout(location = 7) flat in vec4 vPlane;\n" \
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n" \
	"layout(binding = 1) uniform sampler2D uTex;\n" \
	/* [rc4l] The brightmap layer, present on EVERY material -- black where there is none.
	   The brightmap is added to the lit colour, so black is the identity, and that removes the
	   alternative: a per-batch flag, a branch here, and a second pipeline permutation. */ \
	"layout(binding = 5) uniform sampler2D uBrightmap;\n" \
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
	/* [rc4l] A zero normal means the CPU already did this piece's dynamic lighting -- take none here.

	   Sprites carry no normal, because a billboard turns to face the camera and has no side for a
	   light to be in front of or behind. That much was already true; what it also means is that a
	   sprite's dynamic light does not come from this loop at all. gl_SetDynSpriteLight computes it
	   on the CPU and RegisterSprite folds the result into the vertex colour, so adding the lights
	   again here counts them twice AND ignores everything that function knows.

	   DONTLIGHTSELF is what makes that visible. An item does not light itself: gl_SetDynSpriteLight
	   skips a light whose target is the actor being lit, so Freedoom MAP01's armor bonus is supposed
	   to sit DARK in the green pool its own glow throws on the floor. GL draws it that way. This loop
	   knows nothing of owners, lit it with its own light, and the sprite came out green: 48.7 against
	   GL's 15.4 in the green channel, with the floor beneath it agreeing in both.

	   So the marker now carries its full meaning -- no side, and no light from here. */ \
	"    if (dot(vPlane.xyz, vPlane.xyz) <= 0.0001) return base;\n" \
	"    vec3 dyn = vec3(0.0);\n" \
	"    for (int i = 0; i < n; i++) {\n" \
	"        vec4 lp = lights[i*2];\n" \
	"        vec4 lc = lights[i*2+1];\n" \
	"        vec3 d = lp.xyz - vPixelPos.xyz;\n" \
	/* The side test gl_GetLight does per surface: a light behind the plane does not light it.
	   Without this the backs of walls and the room next door get lit, which reads as a scene far
	   more saturated than GL's. */ \
	/* [rc4l] ...and the same test read off the plane instead of the fragment.

	   dot(n, lightPos) - planeD is dot(n, lightPos - fragPos) for any fragment ON the plane, so this
	   is the same quantity, computed from numbers that do not vary across the face. A light lying
	   exactly in the plane gives exactly zero and is KEPT, which is what gl_flats.cpp does: it drops
	   a light only when the plane is strictly on the wrong side of it. */ \
	"        if (dot(vPlane.xyz, lp.xyz) - vPlane.w < 0.0) continue;\n" \
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
	"vec3 fuaShadeEx(vec3 texel, bool wantDynLight) {\n" \
	"    float dbg = uCameraPos.w;\n" \
	"    if (dbg == 2.0) return vec3(gl_FragCoord.z);\n" \
	"    if (dbg == 3.0) return vec3(fract(gl_FragCoord.z * 64.0));\n" \
	"    if (dbg == 4.0) return vColor;\n" \
	"    if (dbg == 10.0) return texel;\n" \
	"    if (dbg == 11.0) return fuaDynLight(vec3(0.0));\n" \
	"    if (dbg == 12.0) return vec3(uLightParams.x / 16.0);\n" \
	"    if (dbg == 13.0) return vec3(lights[0].w / 256.0);\n" \
	/* [rc4l] 14: the surface NORMAL, 15: whether the interpolated world height holds still.

	   fuaDynLight decides whether a light reaches a surface with a SIGN TEST on the interpolated
	   world position, and a sign test is only as trustworthy as its inputs. On a floor the world
	   height is the same number at every fragment, so 15 has to come out flat black. Anything else
	   is the rasteriser handing back a height that wobbles -- and a light sitting AT that height
	   then switches on and off from fragment to fragment as the wobble crosses it. */ \
	"    if (dbg == 14.0) return vNormal * 0.5 + 0.5;\n" \
	"    if (dbg == 15.0) return vec3(fract(vPixelPos.y * 64.0));\n" \
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
	/* [rc4l] Mirror clipping. Everything on the far side of the mirror plane is behind the glass and
	   must not appear in the reflection -- without this the wall the mirror hangs on renders first
	   from the reflected camera and fills the whole reflection. w == 0 means no mirror is active. */ \
	"    if (uClipPlane.w != 0.0 && dot(vPixelPos.xyz, uClipPlane.xyz) + uClipPlane.w < 0.0) discard;\n" \
	"    vec3 color = vColor;\n" \
	"    if (vLightParm.x >= 0.0) color *= 1.0 - R_DoomLightingEquation(vLightParm.x, gl_FragCoord.z);\n" \
	"    else if (fogMode > 0.0)  color  = mix(vec3(0.0), color, fogfactor);\n" \
	/* Dynamic lights add to the light COLOUR before the texture is modulated, exactly as
	   getLightColor does -- adding after would light the black parts of a texture too. */ \
	"    color = min(color, vec3(1.0));\n" \
	/* [rc4l] Brightmaps add to the LIGHT, not to the finished fragment.
	   main.fp calls ProcessLight(color) while `color` is still the light colour -- the texel is
	   multiplied in afterwards -- so a white brightmap texel means "light this texel fully", and the
	   surface keeps its own colour. Adding the same white to texel * color instead means "make this
	   texel white", and a lava floor came out as a washed-out pink slab: luminance 41 -> 162 at the
	   same camera. Right value, right texture, wrong point in the pipeline. */ \
	"    color = min(color + texture(uBrightmap, vUV).rgb, vec3(1.0));\n" \
	/* [rc4l] Dynamic lights are optional, because a SHADED DECAL must not take them.
	   Its colour is its own -- a scorch mark is black -- and that colour arrives folded into the
	   vertex light, so a black decal has vColor 0 and the dynamic light term is then the ONLY thing
	   left in it. Firing a weapon lit every burn mark in the room with the muzzle flash's colour.
	   GL never has this problem: it applies the decal's colour as an OBJECT COLOUR that multiplies
	   the finished fragment, and zero times anything is still zero. */ \
	"    if (wantDynLight) color = fuaDynLight(color);\n" \
	"    vec3 frag = texel * color;\n" \
	"    if (fogMode < 0.0) frag = mix(vFog.rgb, frag, fogfactor);\n" \
	"    return frag;\n" \
	"}\n" \
	"vec3 fuaShade(vec3 texel) { return fuaShadeEx(texel, true); }\n"

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
// [rc4l] The world pass, writing its SURFACE NORMAL out alongside the colour.
//
// This is what makes a projected decal legitimate. A decal reads the depth buffer to find where a
// surface is, and it needs to know which way that surface faces to lay a picture into it. Without a
// normal to read it has to estimate one from neighbouring depth samples, and that estimate is wrong
// in all the places that matter: across a silhouette it straddles two unrelated surfaces, on a
// two-pixel sliver like the front of a low ledge there is no neighbourhood to sample, and at a
// grazing angle it is a difference of nearly-equal large numbers. Every decal artifact reported today
// traces back to that estimate -- a notch cut through a ledge, a mark that changed as the camera
// moved, a scorch smeared into radial streaks.
//
// The estimate was never necessary. This shader has the exact normal already: it arrives per vertex
// from the mesh, correct for walls, flats, slopes and both faces of a 3D floor. Writing it to a
// second target costs one attachment and deletes an entire class of bug.
//
// It also handles moving geometry for free, which the alternative does not: a door's normal is
// written fresh every frame from where the door actually is.
static const char *kScenePSGBuffer =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"layout(location = 1) out vec4 outNormal;\n"
	"void main() {\n"
	"    vec4 t = texture(uTex, vUV);\n"
	"    if (t.a < 0.5) discard;\n"
	"    outColor = vec4(fuaShade(t.rgb), 1.0);\n"
	/* Encoded to 0..1 because the target is 8-bit unorm. That is coarse for lighting and ample here:
	   the decal pass uses this to lay axes into a plane and to ask how far a surface is from the
	   impact, and neither needs better than a degree or so. */
	"    outNormal = vec4(normalize(vNormal) * 0.5 + 0.5, 1.0);\n"
	"}\n";

static const char *kScenePSTrans =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    vec4 t = texture(uTex, vUV);\n"
	"    if (t.a < 0.04) discard;\n"
	"    outColor = vec4(fuaShade(t.rgb), t.a * vLightParm.z);\n"
	"}\n";

// [rc4l] The texture is an ALPHA MASK: its red channel is the shape, the colour is the vertex light.
//
// Shaded decals -- scorch marks, most bullet impacts -- store their silhouette in red and carry
// their colour on the decal itself, which RegisterDecal has already folded into the vertex light.
// Sampling one as an ordinary image reads the red channel as brightness and paints a white blob
// where GL paints a black burn. GL reaches the same result with TM_REDTOALPHA plus an object colour.
static const char *kScenePSRedAlpha =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    float a = texture(uTex, vUV).r * vLightParm.z;\n"
	"    if (a <= 0.0) discard;\n"
	// No dynamic lights: see fuaShadeEx. A scorch mark is black, so its vertex colour is zero, and
	// the dynamic light term would be the only thing left in it -- every burn mark in the room lit
	// up with the muzzle flash's colour when the weapon fired.
	"    outColor = vec4(fuaShadeEx(vec3(1.0), false), a);\n"
	"}\n";


// [rc4l] PROJECTED DECALS.
//
// A decal is a box in the world. The pass draws that box, reads the scene depth under each of its
// fragments, reconstructs where the world surface actually is, and paints it if it falls inside.
// Nothing is glued to a surface, so none of the questions a glued quad forces even exist: no
// coplanar z-fighting, no clearance offset, no depth bias, no ordering against the surface it marks.
// It also lands correctly on stairs, slopes and 3D floors, which a flat quad never can.
//
// The box is drawn with depth test OFF and culling OFF so it works from inside as well as outside --
// walking over a decal must not make it vanish.
// [rc4l] The mark's texture coordinate, measured from where the blast LANDED.
//
// A blast does not project from a plane, it radiates from a point, and everything that went wrong
// before came from pretending otherwise. Projecting from a plane asks every surface to be
// parameterised by an axis chosen before the surface was known. That works on the surface that was
// hit and degenerates on every other one: a floor met at a right angle has no movement at all along
// the projection axis, so one row of texels is dragged across it. Four attempts to paper over that
// each failed somewhere new -- the drag itself, then a black slab where the drag covered the whole
// box, then a hole where the slab was refused, then a wedge of floor that the strip patching the
// corner never reached, because a strip runs parallel to one wall and a floor wraps round at an
// angle.
//
// Measured from the point instead, each surface is parameterised in ITS OWN plane. The normal comes
// out of the depth buffer, the mark's across-axis is turned into that plane to keep the picture the
// right way up, and the coordinate is the fragment's offset from the blast centre along those two.
// Nothing is degenerate at any angle, so there is no surface the mark fails to cover: if it is inside
// the radius it is parameterised, whatever it is. Corners look like soot that travelled because that
// is precisely what is being described -- the same blast, measured on each thing it reached.
//
// This is what the projection was for and could not do, and it is cheaper than what it replaces: no
// unwrap, no carry, no facing fade, no join arithmetic, no per-surface companion boxes.
// [rc4l] Column-major perspective * view, matching the GL renderer's convention in
// FGLRenderer::SetProjection/SetViewMatrix: X east, Y up, Z south, with the pixel-stretch flip.
// [rc4l] fovOverride/aspectOverride are for camera textures, which are rarely the screen's shape and
// carry their own field of view. Zero means "use the screen's", which is every ordinary frame.
static float g_fovOverride = 0.f, g_aspectOverride = 0.f;

static void BuildMVP(float *m)
{
	float fovY = 74.0f * 3.14159265f / 180.0f;
	if (g_fovOverride > 0.f) fovY = g_fovOverride;
	// [rc4l] From the swapchain, not a constant. A hard-coded 16:10 meant the backend framed the
	// world differently from the engine window, so a screenshot pair could never be compared
	// pixel-for-pixel -- the same screen position was a different part of the level in each.
	float aspect = 16.0f / 10.0f;
	if (g_aspectOverride > 0.f) aspect = g_aspectOverride;
	else if (auto *swap = GetSwapChain())
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
// [rc4l] r,g,b,useTex carry the sky CAP.
//
// The dome only spans 60 degrees of elevation; above that there is a hole, and GZDoom fills it with
// a flat-coloured fan in the sky texture's average top colour (GetSkyCapColor). Without it the hole
// showed the backend's clear colour -- a dark navy disc straight overhead, which is what "the very
// top of the sky is missing the coloured thing GL has" was.
//
// useTex is 1 for the textured dome and 0 for the caps, so one pipeline draws both.
// [rc4l] alpha is separate from useTex because the dome's TOP ROW is translucent.
//
// GZDoom gives row 0 a vertex colour with zero alpha (SkyVertex: `r == 0 ? 0xffffff : 0xffffffff`),
// so the texture fades out toward the zenith and blends into the flat cap underneath. Without it the
// cap meets the dome at a hard circular seam -- a black disc stamped on the sky, which is what kept
// getting reported as missing fog.
struct SkyVertexData { float x, y, z, u, v, r, g, b, useTex, alpha; };

static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyPSO;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyFadePSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyFadeVB;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skySRB;
static int          g_skyVerts = 0;
static const void  *g_skyMaterial = NULL;
static int          g_skyBuiltFor = -1;   // FTextureID index; == has ambiguous overloads
static bool         g_skyBuiltValid = false;

static const char *kSkyVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aSkyColor;\n"
	"layout(location = 3) in float aSkyAlpha;\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec4 vSkyColor;\n"
	"layout(location = 2) out float vSkyAlpha;\n"
	"void main() {\n"
	// Centred on the camera, so the dome is effectively at infinity.
	//
	// [rc4l] The sky also SCROLLS. GLSkyInfo::x_offset carries mSky1Pos, which advances with
	// wall-clock time, and RenderDome folds it into the dome's Y rotation together with its constant
	// -180. Baking that into the vertices would mean rebuilding the buffer every frame, so it comes
	// in as an angle and is applied here.
	"    float sa = sin(uLightParams.y), ca = cos(uLightParams.y);\n"
	"    vec3 p = vec3(aPos.x * ca + aPos.z * sa, aPos.y, -aPos.x * sa + aPos.z * ca);\n"
	"    gl_Position = uMVP * vec4(p + uCameraPos.xyz, 1.0);\n"
	"    vUV = aUV;\n"
	"    vSkyColor = aSkyColor;\n"
	"    vSkyAlpha = aSkyAlpha;\n"
	"}\n";

static const char *kSkyPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(location = 1) in vec4 vSkyColor;\n"
	"layout(location = 2) in float vSkyAlpha;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	// vSkyColor.a selects: 1 takes the texture (the dome), 0 takes the flat colour (the caps).
	"void main() {\n"
	// [rc4l] vSkyColor.a does three jobs, so the fade layer needs no extra vertex attribute:
	//   1  textured, opaque -- the dome
	//   0  flat colour, opaque -- the caps
	//  <0  flat colour, alpha = -a -- the sky fade layer GL draws over the whole sky
	"    float sel = max(vSkyColor.a, 0.0);\n"
	"    float al = vSkyColor.a < 0.0 ? -vSkyColor.a : 1.0;\n"
	"    outColor = vec4(mix(vSkyColor.rgb, texture(uTex, vUV).rgb, sel), al * vSkyAlpha);\n"
	"}\n";

// [rc4l] One sky dome vertex, with RenderDome's model and texture transforms folded in.
//
// modelScaleY/modelTransY are the model matrix (which moves the dome in world space) and vscale is
// the texture matrix's V scale. These are three separate things in the engine and were one here,
// which only works for the short skies Doom itself ships.
static void SkyVertexAt(int r, int c, int rows, int cols, bool yflip, float modelScaleY,
	float modelTransY, float xscale, float vscale, SkyVertexData &out)
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

	// [rc4l] Only the vertex negation, NOT RenderDome's additional -180 degree Y rotation.
	//
	// Composing both (which leaves x alone and negates z) was tried and measured worse -- 18.5 against
	// 16.7 on gvh04 -- so the rotation is evidently already accounted for by the view basis this
	// backend uses. Recorded because it is the obvious "fix" to try, and it is wrong.
	out.x = -x;
	out.y = (y - 1.0f) * modelScaleY + modelTransY;   // RenderDome's scale-then-translate
	out.z = z;
	out.u = (-(float)c / cols) * xscale;
	out.v = (yflip ? (1.0f + (float)(rows - r) / rows) : ((float)r / rows)) * vscale;
}

// The current sky texture's SkyOffset, captured in EnsureSky where the FTexture is in hand.
static int g_skyTexOffset = 0;
// [0] top cap, [1] bottom cap -- GetSkyCapColor(false/true), captured where the FTexture is in hand.
static PalEntry g_skyCapColor[2];
// Radians, recomputed per frame: -180 degrees (RenderDome's constant) plus the scroll offset.
static float g_skyAngle = 0.f;
// The dome's texture scales, so a ray that misses samples the sky the same way the dome draws it.
static float g_skyXScale = 4.f, g_skyVScale = 1.f;
// Mirror clip plane in mesh space (nx, ny, nz, d); all zero disables it.
static float g_clipPlane[4] = { 0.f, 0.f, 0.f, 0.f };

static void BuildSkyDome(int texw, int texh)
{
	const int rows = 4, cols = 128;

	// [rc4l] RenderDome's four height cases, in full.
	//
	// Only the short-sky branch was implemented, and the -1250 world translate that goes with it was
	// applied unconditionally. Doom's own skies are 256x128 so they land in that branch and looked
	// right, which is exactly why this survived: every stock map agreed with GL. A mod with a TALL
	// sky -- Ghouls vs Humans among them -- takes one of the other two branches, where both the scale
	// and the translate are different, and its sky came out showing the wrong band of the texture.
	//
	// A tall sky also needs the texture matrix's V scale, which is a separate thing from the model
	// matrix's Y scale and was not represented here at all.
	const float skyoffsetfactor = 57.f;
	float modelScaleY = 1.f, modelTransY = 0.f, vscale = 1.f;
	if (texh < 128)
	{
		modelTransY = -1250.f; modelScaleY = 128 / 230.f;
		vscale = (texh > 0) ? (float)(128 / texh) : 1.f;   // integer division, as upstream
	}
	else if (texh < 200)
	{
		modelTransY = -1250.f; modelScaleY = texh / 230.f;
	}
	else if (texh <= 240)
	{
		modelTransY = (200 - texh + g_skyTexOffset + skyoffset) * skyoffsetfactor;
		modelScaleY = 1.f + ((texh - 200.f) / 200.f) * 1.17f;
	}
	else
	{
		modelTransY = (-40 + g_skyTexOffset + skyoffset) * skyoffsetfactor;
		modelScaleY = 1.2f * 1.17f;
		vscale = (texh > 0) ? 240.f / texh : 1.f;
	}
	const float xscale = texw > 0 ? 1024.f / texw : 1.f;
	g_skyXScale = xscale; g_skyVScale = vscale;

	TArray<SkyVertexData> verts;
	for (int hemi = 0; hemi < 2; hemi++)
	{
		const bool yflip = (hemi == 1);

		// [rc4l] Cap the hole the dome leaves -- BEFORE the strips, which is what RenderDome does.
		//
		// The dome only reaches 60 degrees of elevation, so straight up there is nothing, and what
		// showed through was the backend's clear colour: a dark navy disc overhead on every open map.
		// GZDoom fills it with a flat fan in the sky texture's own average edge colour.
		//
		// The order is not incidental. The cap is a flat lid at row 1's height, so from the centre it
		// subtends everything from 45 degrees up -- it overlaps the textured 45-to-60 band rather than
		// merely abutting it. Drawn afterwards, with no depth test on the sky, it painted flat colour
		// over a third of the visible sky: a huge black disc where GL has cloud. Drawn first, the
		// strips cover it back up and only the part above the dome remains.
		{
			const PalEntry cap = g_skyCapColor[hemi ? 1 : 0];
			SkyVertexData centre;
			SkyVertexAt(1, 0, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, centre);
			for (int c = 1; c < cols; c++)
			{
				SkyVertexData a, b;
				SkyVertexAt(1, c,     rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, a);
				SkyVertexAt(1, c + 1, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, b);
				SkyVertexData tri[3] = { centre, a, b };
				for (int k = 0; k < 3; k++)
				{
					tri[k].r = cap.r / 255.f; tri[k].g = cap.g / 255.f; tri[k].b = cap.b / 255.f;
					tri[k].useTex = 0.f;
					tri[k].alpha = 1.f;
					tri[k].y = centre.y;   // a lid, not part of the dome's curve
				}
				verts.Push(tri[0]); verts.Push(tri[1]); verts.Push(tri[2]);
			}
		}

		for (int r = 0; r < rows; r++)
		{
			for (int c = 0; c < cols; c++)
			{
				SkyVertexData q[4];
				SkyVertexAt(r,     c,     rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[0]);
				SkyVertexAt(r + 1, c,     rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[1]);
				SkyVertexAt(r,     c + 1, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[2]);
				SkyVertexAt(r + 1, c + 1, rows, cols, yflip, modelScaleY, modelTransY, xscale, vscale, q[3]);
				// Row 0 is the zenith edge and fades out, exactly as SkyVertex's vertex colour does.
				for (int k = 0; k < 4; k++) { q[k].r = q[k].g = q[k].b = 1.f; q[k].useTex = 1.f; }
				q[0].alpha = q[2].alpha = (r == 0) ? 0.f : 1.f;
				q[1].alpha = q[3].alpha = 1.f;
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
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
	};
	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	samp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
	samp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
	samp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;   // no wrap top-to-bottom
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
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
	// Blended, so the dome's fading top row composites over the cap drawn beneath it.
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
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
	// [rc4l] TexMan(id), not TexMan[id]. The subscript returns the texture as authored; the call
	// applies the animation translation, which is what the engine's own sky path uses.
	//
	// GvH's skies are animated sequences, so with the subscript the backend pinned frame one forever
	// while GL cycled: the engine reported drawing SKY_F049 and this drew SKY_F001. It reads as
	// "the wrong sky for this map" rather than "a frozen animation", because the frames of a
	// cloudscape do not look like frames of anything -- they just look like a different sky.
	FTextureID id = sky1texture;
	FTexture *raw = id.isValid() ? TexMan(id) : NULL;
	FMaterial *mat = raw ? FMaterial::ValidateTexture(raw, false) : NULL;
	if (mat == NULL) { g_skyBuiltValid = false; return; }

	// [rc4l] Keyed on the RESOLVED material, so an animation frame counts as a change -- and only the
	// binding is redone when the geometry cannot have moved. An animated sky advances several times a
	// second; rebuilding a 128-column dome and reuploading it that often would be pure waste.
	if (g_skyBuiltValid && g_skyMaterial == (const void *)mat) return;
	const bool sameShape = g_skyBuiltValid && g_skyBuiltFor == mat->TextureHeight();
	g_skyBuiltFor = mat->TextureHeight();

	g_skyTexOffset = (mat->tex != NULL) ? mat->tex->SkyOffset : 0;
	g_skyCapColor[0] = (mat->tex != NULL) ? mat->tex->GetSkyCapColor(false) : PalEntry(0);
	g_skyCapColor[1] = (mat->tex != NULL) ? mat->tex->GetSkyCapColor(true)  : PalEntry(0);
	// [rc4l] Say what the sky actually IS, once per genuinely new texture rather than per animation
	// frame. Comparing this line against the engine's own is what identified both the skybox that is
	// unimplemented and the animation that was frozen.
	if (!sameShape)
	{
		Printf("vulkan sky: %s %dx%d skyoffset %d  cap %d,%d,%d / %d,%d,%d%s\n",
			(mat->tex != NULL && mat->tex->Name != NULL) ? mat->tex->Name : "?",
			mat->TextureWidth(), mat->TextureHeight(), g_skyTexOffset,
			g_skyCapColor[0].r, g_skyCapColor[0].g, g_skyCapColor[0].b,
			g_skyCapColor[1].r, g_skyCapColor[1].g, g_skyCapColor[1].b,
			(mat->tex != NULL && mat->tex->gl_info.bSkybox) ? "  [SKYBOX -- not implemented]" : "");
	}
	if (!sameShape) BuildSkyDome(mat->TextureWidth(), mat->TextureHeight());
	g_skyMaterial = mat;
	if (auto *v = g_skySRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(GetMaterialSRV(g_skyMaterial, 0));
	g_skyBuiltValid = true;
}

// [rc4l] The sky's fade layer: a translucent sheet in the sector's fade colour, over the sky and
// under the world.
//
// GL draws this and this backend did not, so a foggy map's sky came out as a black hole where GL had
// haze -- reported as "fog missing".
//
// It gets its own shader pair rather than riding the dome's. GL's version is four triangles a single
// unit across, drawn with depth clamping so that a shape entirely inside the near plane still covers
// the screen. Reproducing that by placing world-space geometry is fighting the projection: the first
// attempt put a horizontal sheet 60000 units overhead, which is a ceiling rather than something that
// encloses the camera, and covered nothing but the top of the frame. A quad written straight into
// clip space is what the thing actually is.
static const char *kSkyFadeVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aSkyColor;\n"
	"layout(location = 0) out vec4 vFade;\n"
	"void main() {\n"
	"    vFade = vec4(aSkyColor.rgb, -aSkyColor.a);\n"
	"    gl_Position = vec4(aPos.xy, 0.0, 1.0);\n"
	"}\n";

static const char *kSkyFadePS =
	"#version 450\n"
	"layout(location = 0) in vec4 vFade;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vFade; }\n";

static bool EnsureSkyFadePipeline()
{
	if (g_skyFadePSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
		ci.Desc.Name = "fua sky fade VS";
		ci.Source = kSkyFadeVS;
		dev->CreateShader(ci, &vs);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua sky fade PS";
		ci.Source = kSkyFadePS;
		dev->CreateShader(ci, &ps);
	}
	if (!vs || !ps) return false;

	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 4, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
	};
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua sky fade PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	{
		auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
		rt.BlendEnable = true;
		rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
		rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOp = Diligent::BLEND_OPERATION_ADD;
		rt.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
		rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
		rt.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
	}
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = 4;
	dev->CreateGraphicsPipelineState(pci, &g_skyFadePSO);
	return g_skyFadePSO.RawPtr() != nullptr;
}

static void DrawSkyFade(Diligent::IDeviceContext *ctx)
{
	float fr, fg, fb, fa;
	zx::hwrender::GetSkyFog(fr, fg, fb, fa);
	if (fa <= 0.f) return;
	if (!EnsureSkyFadePipeline()) return;

	SkyVertexData v[6];
	const float qx[6] = { -1.f, -1.f,  1.f,  1.f, -1.f,  1.f };
	const float qy[6] = { -1.f,  1.f, -1.f,  1.f,  1.f, -1.f };
	for (int i = 0; i < 6; i++)
	{
		v[i].x = qx[i]; v[i].y = qy[i]; v[i].z = 0.f;
		v[i].u = 0.f; v[i].v = 0.f;
		v[i].r = fr; v[i].g = fg; v[i].b = fb;
		v[i].useTex = -fa;   // the fade VS negates it back into an alpha
		v[i].alpha = 1.f;
	}

	if (!g_skyFadeVB)
	{
		Diligent::BufferDesc bd;
		bd.Name = "fua sky fade VB";
		bd.Size = sizeof(v);
		bd.Usage = Diligent::USAGE_DEFAULT;
		bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		GetDevice()->CreateBuffer(bd, nullptr, &g_skyFadeVB);
		if (!g_skyFadeVB) return;
	}
	ctx->UpdateBuffer(g_skyFadeVB, 0, sizeof(v), v,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	Diligent::IBuffer *vbs[] = { g_skyFadeVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_skyFadePSO);
	Diligent::DrawAttribs d;
	d.NumVertices = 6;
	d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(d);
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

	DrawSkyFade(ctx);
}

// [rc4l] Draw this frame's dynamic geometry -- sprites.
//
// Rebuilt and re-uploaded every frame on purpose: billboards are view-dependent, so there is nothing
// to cache. The cost is bounded by what is on screen (a few hundred quads), and at ~5.6M triangles
// per GPU millisecond the upload dominates the draw by a wide margin.
//
// Drawn AFTER the world with the alpha-tested pipeline and normal depth, so sprites occlude and are
// occluded correctly. Only the opaque sprites are drawn here: the translucent ones are handed to the
// world's translucent pass so the two sort against each other -- an item lying under a glass 3D floor
// has to be composited before the floor, and it cannot be if sprites are a separate pass afterwards.
static void BuildDynamic(Diligent::IDeviceContext *ctx)
{
	g_dynDraws = g_dynTris = 0;
	g_dynByBlend[0] = g_dynByBlend[1] = g_dynByBlend[2] = g_dynByBlend[3] = 0;
	g_dynReady = false;

	int nverts = 0, npieces = 0;
	const FFlatVertex *src = zx::levelmesh::DynVertices(nverts);
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(npieces);
	if (src == NULL || nverts <= 0 || pieces == NULL || npieces <= 0) { g_dynRuns.Clear(); return; }

	static TArray<SceneVertex> vb;
	TArray<DynRun> &runs = g_dynRuns;

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
		int curTrans = -99999;
		bool curBias = false;
		bool curRed = false;
		for (int i = 0; i < npieces; i++)
		{
			const zx::levelmesh::MeshPiece &p = pieces[order[i]];
			if (p.range.count == 0) continue;
			// [rc4l] Every dynamic piece is its own run.
			//
			// Sprites are all drawn in one back-to-front pass now, the way GL does it, so any two
			// merged into a shared run could no longer be ordered against each other. There are a few
			// hundred sprites in a busy frame, so the batching this gives up is not worth having.
			{
				DynRun r; r.material = p.material; r.first = vb.Size(); r.count = 0; r.blend = p.blendMode;
				r.translation = p.translation;
				r.depthBias = p.depthBias;
				r.redAlpha = p.redToAlpha;
				r.cx = p.sortX; r.cy = p.sortY; r.cz = p.sortZ;
				runs.Push(r);
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
	g_dynReady = true;
}

// [rc4l] The opaque half of the sprite stream. Alpha-tested, depth written, so these behave exactly
// like world geometry against everything drawn afterwards.
static void DrawDynamicOpaque(Diligent::IDeviceContext *ctx)
{
	if (!g_dynReady) return;

	Diligent::IBuffer *vbs[] = { g_dynVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_maskedNoCullPSO);
	Diligent::IPipelineState *boundOpaque = g_maskedNoCullPSO.RawPtr();

	for (unsigned i = 0; i < g_dynRuns.Size(); i++)
	{
		const DynRun &r = g_dynRuns[i];
		if (r.count == 0 || r.blend != 0) continue;
		Diligent::IPipelineState *pso = r.depthBias ? g_maskedDecalPSO.RawPtr()
		                                            : g_maskedNoCullPSO.RawPtr();
		auto *srb = GetMaterialSRB(pso, r.material, r.translation);
		if (!srb) continue;
		if (pso != boundOpaque) { ctx->SetPipelineState(pso); boundOpaque = pso; }
		ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::DrawAttribs draw;
		draw.NumVertices = r.count;
		draw.StartVertexLocation = r.first;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
		g_dynDraws++;
		g_dynTris += r.count / 3;
		g_dynByBlend[0]++;
	}
}

// [rc4l] The away-facing half of a 3D floor used to be dropped HERE, per surface, on the CPU.
//
// That cost a batch per plane -- 240 extra draws on dbab02 -- because whether a horizontal plane
// faces the camera depends on its own height, so planes at different heights cannot share one. It is
// gone: once flats are wound according to their plane's normal, ordinary back-face culling drops
// exactly the same surfaces for nothing, and disabling the CPU test changed the frame by 0.6%.
//
// The dependency is worth stating plainly: with fua_dg_cull 0 the thin-3D-floor z-fighting and the
// double-composited translucent floor both come back, because nothing else removes the far face.

// [rc4l] Everything that blends, world and sprites together, in one back-to-front pass.
//
// They cannot be two passes. A rocket box lying on the lava under a glass 3D floor has to be
// composited BEFORE the floor is; if sprites are drawn afterwards as their own pass it paints over
// the floor and appears to float on top of it, which is precisely what this looked like. The world's
// contribution is a batch in the static vertex buffer and a sprite's is a run in the dynamic one, so
// the merged list carries which buffer it came from and the pass rebinds when it crosses over --
// a few dozen draws, so the extra binds are not worth avoiding.
static void DrawBlended(Diligent::IDeviceContext *ctx)
{
	struct BlendDraw { bool dyn; unsigned first, count; int blend, translation; const void *material;
	                   Diligent::IShaderResourceBinding *srb; float dist; bool bias, red; };
	static TArray<BlendDraw> list;
	list.Clear();

	const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);

	for (unsigned i = 0; i < g_blendBatches.Size(); i++)
	{
		const SceneBatch &b = g_batches[g_blendBatches[i]];
		if (b.count == 0 || b.srb == NULL) continue;
		const float dx = b.sortX - cx, dy = b.sortY - cy, dz = b.sortZ - cz;
		BlendDraw d;
		d.dyn = false; d.first = b.first; d.count = b.count; d.blend = b.blend;
		d.translation = 0; d.material = b.material; d.srb = b.srb;
		d.bias = false; d.red = false;
		d.dist = dx*dx + dy*dy + dz*dz;
		list.Push(d);
	}

	if (g_dynReady)
	{
		for (unsigned i = 0; i < g_dynRuns.Size(); i++)
		{
			const DynRun &r = g_dynRuns[i];
			if (r.count == 0) continue;
			// Blend mode 3 is the fuzz shadow; the engine draws it as a dark near-opaque overlay, and
			// normal translucency is a fair stand-in until the fuzz shaders are ported.
			// [rc4l] blend 0 takes the TRANSLUCENT pipeline, not the masked one.
			//
			// An alpha-1 sprite blends identically to an opaque draw and discards its transparent
			// texels just the same, but it does not write depth -- which is the whole point. GL's
			// sprites live in one sorted list with depth writes off, and splitting them into an
			// opaque depth-writing pass let an alpha-tested impact sprite occlude the additive glow
			// behind it: black holes where a plasma burst's bright core should be.
			Diligent::IPipelineState *pso = r.redAlpha
				? ((r.blend == 2) ? g_addRedAlphaPSO.RawPtr() : g_transRedAlphaPSO.RawPtr())
				: r.depthBias
					? ((r.blend == 2) ? g_addDecalPSO.RawPtr()  : g_transDecalPSO.RawPtr())
					: ((r.blend == 2) ? g_addNoCullPSO.RawPtr() : g_transNoCullPSO.RawPtr());
			auto *srb = GetMaterialSRB(pso, r.material, r.translation);
			if (!srb) continue;
			BlendDraw d;
			d.dyn = true; d.first = r.first; d.count = r.count; d.blend = r.blend;
			d.translation = r.translation; d.material = r.material; d.srb = srb;
			d.bias = r.depthBias;
			d.red = r.redAlpha;
			const float dx = r.cx - cx, dy = r.cy - cy, dz = r.cz - cz;
			d.dist = dx*dx + dy*dy + dz*dz;
			// [rc4l] A decal sorts as very slightly FARTHER than it is.
			//
			// It is paint on a surface, so anything standing in front of that surface must be drawn
			// over it. A flamethrower's fire sprite hovers a few units above the floor it is scorching
			// and can easily sit at almost the same distance as the mark, and with a plain
			// farthest-first sort the decal then lands second and buries the sprite. Two percent is
			// proportional, so it only ever decides a near-coincident pair and never reorders anything
			// genuinely in front of or behind.
			//
			// AFTER the distance is computed, which it was not: the multiply sat one line above the
			// assignment that overwrote it, so the rule had never once applied. A bias that is dead
			// looks exactly like a bias that is too small -- the pair it was meant to settle simply
			// keeps trading places -- which is the worst way for a fix to fail.
			if (r.depthBias) d.dist *= 1.02f;
			list.Push(d);
			g_dynDraws++;
			g_dynTris += r.count / 3;
			if (r.blend >= 0 && r.blend < 4) g_dynByBlend[r.blend]++;
		}
	}

	if (list.Size() == 0) return;
	// [rc4l] Farthest first, and DETERMINISTIC on a tie.
	//
	// std::sort is not stable, so two draws at the same distance -- a decal and the one a template
	// puts underneath it, landing at the same point -- traded places between frames and flickered
	// through each other. Falling back to the buffer offset makes equal distances resolve the same
	// way every frame.
	std::sort(&list[0], &list[0] + list.Size(),
		[](const BlendDraw &a, const BlendDraw &b) {
			if (a.dist != b.dist) return a.dist > b.dist;
			// [rc4l] At equal distance, ADDITIVE draws last.
			//
			// Additive blending only ever brightens, so nothing can meaningfully be drawn over it --
			// but it can very easily be drawn UNDER something and lost. A decal pair lands at exactly
			// one point: a dark scorch and the glow that belongs on top of it. Ordered by capture
			// alone the scorch could land second and bury the glow, which is what "the scorch is
			// overriding the glow" was. Additive last is order-independent for the additive draws
			// themselves, so this costs nothing and settles the pair.
			const int aa = (a.blend == 2) ? 1 : 0, ba = (b.blend == 2) ? 1 : 0;
			if (aa != ba) return aa < ba;
			return a.first < b.first;
		});

	Diligent::IPipelineState *bound = NULL;
	int boundVB = -1;
	const Diligent::Uint64 offsets[] = { 0 };
	for (unsigned i = 0; i < list.Size(); i++)
	{
		const BlendDraw &d = list[i];
		const int want = d.dyn ? 1 : 0;
		if (want != boundVB)
		{
			Diligent::IBuffer *vbs[] = { d.dyn ? g_dynVB.RawPtr() : g_vb.RawPtr() };
			if (!vbs[0]) continue;
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			boundVB = want;
		}
		Diligent::IPipelineState *pso = !d.dyn
			? ((d.blend == 2) ? g_addPSO.RawPtr()       : g_transPSO.RawPtr())
			: d.red
				? ((d.blend == 2) ? g_addRedAlphaPSO.RawPtr() : g_transRedAlphaPSO.RawPtr())
				: d.bias
					? ((d.blend == 2) ? g_addDecalPSO.RawPtr()  : g_transDecalPSO.RawPtr())
					: ((d.blend == 2) ? g_addNoCullPSO.RawPtr() : g_transNoCullPSO.RawPtr());
		if (!pso) continue;
		if (pso != bound) { ctx->SetPipelineState(pso); bound = pso; }
		ctx->CommitShaderResources(d.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::DrawAttribs draw;
		draw.NumVertices = d.count;
		draw.StartVertexLocation = d.first;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
	}
}

// [rc4l] Drop the pipelines and everything bound to them, so the next upload rebuilds with current
// settings. SRBs are created from a PSO and cannot outlive it.
static int g_builtCull = -1;
static int g_builtFilter = -1;
static void ReleaseDeferredDecalPass();

static void ReleaseScenePipelines()
{
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	for (unsigned i = 0; i < g_batches.Size(); i++) g_batches[i].srb = NULL;
	g_srb.Release();
	g_srbMasked.Release();
	g_skySRB.Release();
	g_skyPSO.Release();
	g_skyFadePSO.Release();
	g_scenePSO.Release();
	g_maskedPSO.Release();
	g_maskedGBufPSO.Release();
	g_maskedNoCullPSO.Release();
	g_transNoCullPSO.Release();
	g_addNoCullPSO.Release();
	g_maskedDecalPSO.Release();
	g_transDecalPSO.Release();
	g_addDecalPSO.Release();
	g_transRedAlphaPSO.Release();
	g_addRedAlphaPSO.Release();
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
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
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
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
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
			if (q.kind == 1)
			{
				// [rc4l] A line, widened into a quad along its own perpendicular. Doing it here rather
				// than with a line topology keeps every 2D record in one buffer and one draw order,
				// which is what a painter's-algorithm layer needs.
				const float dx = q.lx2 - q.x, dy = q.ly2 - q.y;
				const float len = sqrtf(dx * dx + dy * dy);
				const float nx = (len > 0.0001f) ? (-dy / len) * 0.5f : 0.5f;
				const float ny = (len > 0.0001f) ? ( dx / len) * 0.5f : 0.0f;
				v[0].x = q.x   + nx; v[0].y = q.y   + ny;
				v[1].x = q.x   - nx; v[1].y = q.y   - ny;
				v[2].x = q.lx2 + nx; v[2].y = q.ly2 + ny;
				v[3].x = q.lx2 - nx; v[3].y = q.ly2 - ny;
				for (int k = 0; k < 4; k++) { v[k].u = 0.f; v[k].v = 0.f; }
			}
			else
			{
			v[0].x = q.x;       v[0].y = q.y;       v[0].u = q.u1; v[0].v = q.v1;
			v[1].x = q.x;       v[1].y = q.y + q.h; v[1].u = q.u1; v[1].v = q.v2;
			v[2].x = q.x + q.w; v[2].y = q.y;       v[2].u = q.u2; v[2].v = q.v1;
			v[3].x = q.x + q.w; v[3].y = q.y + q.h; v[3].u = q.u2; v[3].v = q.v2;
			}
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
void FillSamplerFromEngine(Diligent::SamplerDesc &samp)
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

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, psOpaque, psMasked, psTrans, psRedAlpha, psGBuffer;
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
		ci.Desc.Name = "fua scene PS masked + gbuffer";
		ci.Source = kScenePSGBuffer;
		dev->CreateShader(ci, &psGBuffer);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua scene PS translucent";
		ci.Source = kScenePSTrans;
		dev->CreateShader(ci, &psTrans);
		ci.Source = kScenePSRedAlpha;
		dev->CreateShader(ci, &psRedAlpha);
	}
	if (!vs || !psOpaque || !psMasked || !psTrans || !psRedAlpha)
	{ err = "scene shader compilation failed"; return false; }

	Diligent::BufferDesc cbd;
	cbd.Name = "fua scene constants";
	// mat4 uMVP + vec4 uCameraPos + vec4 uLightParams + vec4 uClipPlane (mirrors) + vec4 uScreen
	cbd.Size = sizeof(float) * 36;
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
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	static Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	static Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
	};

	// [rc4l] Four pipelines over the same vertex layout and resources:
	//   0 opaque        no discard, depth write -- early-Z survives
	//   1 masked        alpha test, depth write -- the world and opaque sprites
	//   2 translucent   src-alpha blend, depth test but NO depth write
	//   3 additive      src-alpha / one, likewise
	// Blended geometry must not write depth, or a nearer translucent sprite would occlude the one
	// behind it instead of letting it show through.
	// Passes 0..3 are the world's, 4..6 repeat masked/translucent/additive with culling off for
	// sprites. See g_maskedNoCullPSO.
	// Passes 0..3 world, 4..6 sprites (no culling), 7..9 decals (no culling + depth bias).
	// 10..11 are the alpha-mask decal variants (translucent and additive).
	for (int pass = 0; pass < 13; pass++)
	{
		static const char *kNames[13] = { "fua scene PSO opaque", "fua scene PSO masked",
		                                  "fua scene PSO translucent", "fua scene PSO additive",
		                                  "fua sprite PSO masked", "fua sprite PSO translucent",
		                                  "fua sprite PSO additive",
		                                  "fua decal PSO masked", "fua decal PSO translucent",
		                                  "fua decal PSO additive",
		                                  "fua decal PSO redalpha translucent",
		                                  "fua decal PSO redalpha additive",
		                                  "fua scene PSO masked + gbuffer" };
		// 4..6 and 7..9 both map onto shapes 1..3.
		// [rc4l] Pass 12 is pass 1 over again with the G-buffer attached -- see kScenePSGBuffer.
		// Only the OPAQUE world runs before the decals, and it runs through one pipeline, so this is
		// one extra pipeline rather than a second set of twelve.
		const bool gbuffer = (pass == 12);
		const int shape = gbuffer ? 1 : ((pass < 4) ? pass : ((pass < 7) ? pass - 3 :
		                  ((pass < 10) ? pass - 6 : pass - 8)));   // 10->2 trans, 11->3 additive
		const bool noCull = (pass >= 4 && pass < 12);
		const bool decal  = (pass >= 7 && pass < 12);
		const bool redAlpha = (pass >= 10 && pass < 12);
		const bool blended = (shape >= 2);

		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = kNames[pass];
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = gbuffer ? 2 : 1;
		pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
		if (gbuffer) pci.GraphicsPipeline.RTVFormats[1] = Diligent::TEX_FORMAT_RGBA8_UNORM;
		pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = noCull ? Diligent::CULL_MODE_NONE :
			(fua_dg_cull == 1) ? Diligent::CULL_MODE_BACK :
			(fua_dg_cull == 2) ? Diligent::CULL_MODE_FRONT : Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = !blended;
		if (decal)
		{
			// [rc4l] Slope-scaled dominates here. The constant term is expressed in units of the
			// depth format's smallest resolvable difference, which for a FLOAT depth buffer varies
			// with distance and is close to meaningless as a fixed number; the slope-scaled term
			// scales with how obliquely the surface is viewed, which is exactly when a coplanar quad
			// fights. GL's own decal offset is (-1, -128) in the same two roles.
			pci.GraphicsPipeline.RasterizerDesc.DepthBias = -1;
			pci.GraphicsPipeline.RasterizerDesc.SlopeScaledDepthBias = -128.f;
		}
		if (blended)
		{
			auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
			rt.BlendEnable = true;
			rt.SrcBlend  = Diligent::BLEND_FACTOR_SRC_ALPHA;
			rt.DestBlend = (shape == 3) ? Diligent::BLEND_FACTOR_ONE
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
		pci.PSODesc.ResourceLayout.NumVariables = 2;
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
		pci.pVS = vs;
		pci.pPS = gbuffer ? psGBuffer : redAlpha ? psRedAlpha :
			(shape == 0) ? psOpaque : (shape == 1) ? psMasked : psTrans;

		Diligent::RefCntAutoPtr<Diligent::IPipelineState> made;
		dev->CreateGraphicsPipelineState(pci, &made);
		if (pass == 0)      g_scenePSO  = made;
		else if (pass == 1) g_maskedPSO = made;
		else if (pass == 2) g_transPSO  = made;
		else if (pass == 3) g_addPSO    = made;
		else if (pass == 4) g_maskedNoCullPSO = made;
		else if (pass == 5) g_transNoCullPSO  = made;
		else if (pass == 6) g_addNoCullPSO    = made;
		else if (pass == 7) g_maskedDecalPSO  = made;
		else if (pass == 8)  g_transDecalPSO    = made;
		else if (pass == 9)  g_addDecalPSO      = made;
		else if (pass == 10) g_transRedAlphaPSO = made;
		else if (pass == 11) g_addRedAlphaPSO   = made;
		else                 g_maskedGBufPSO    = made;
	}
	if (!g_scenePSO || !g_maskedPSO || !g_transPSO || !g_addPSO ||
		!g_maskedNoCullPSO || !g_transNoCullPSO || !g_addNoCullPSO ||
		!g_maskedDecalPSO || !g_transDecalPSO || !g_addDecalPSO ||
		!g_transRedAlphaPSO || !g_addRedAlphaPSO || !g_maskedGBufPSO)
	{ err = "scene pipeline creation failed"; return false; }

	// [rc4l] Both stages read Constants now -- the pixel shader needs uCameraPos for radial fog.
	// A stage that declares the block but never gets it bound reads garbage rather than failing.
	// [rc4l] The sprite pipelines belong in here too. They were left out when they were added and the
	// backend died on launch with "No resource is assigned to static shader variable 'Constants'" --
	// a pipeline that is created but never has its statics bound is not a working pipeline.
	Diligent::IPipelineState *psos[] = { g_scenePSO, g_maskedPSO, g_transPSO, g_addPSO,
	                                     g_maskedNoCullPSO, g_transNoCullPSO, g_addNoCullPSO,
	                                     g_maskedDecalPSO, g_transDecalPSO, g_addDecalPSO,
	                                     g_transRedAlphaPSO, g_addRedAlphaPSO, g_maskedGBufPSO };
	const Diligent::SHADER_TYPE stages[] = { Diligent::SHADER_TYPE_VERTEX, Diligent::SHADER_TYPE_PIXEL };
	for (int i = 0; i < 13; i++)
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
static bool EnsureMirrorResources();
static bool EnsureAccelerationStructure();
static void ReleaseMirrorBinding();

static bool RayTracingAvailable();

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
	// [rc4l] Opaque first, then blended. Blending is not commutative, so a translucent surface has to
	// be drawn after everything it shows through -- a 3D floor of grating over a lava pit reads as
	// solid grating otherwise, which is what dbab01 looked like until this existed.
	//
	// The secondary key is baseTex, and it is not cosmetic. Two surfaces with DIFFERENT base textures
	// can resolve to the SAME material at bake time, because an animation is at some frame when the
	// mesh is built: a lava floor showing frame 2 and another animated flat also showing frame 2 are
	// one FMaterial. Merged into one batch, they then get re-resolved every frame from whichever
	// baseTex the batch happened to record, and the other surface is repainted with a texture that has
	// nothing to do with it -- which is a lava floor turning into green foliage, at dbab02-flatswap.
	std::sort(&order[0], &order[0] + npieces, [pieces](int a, int b) {
		const int ba = pieces[a].blendMode != 0, bb = pieces[b].blendMode != 0;
		if (ba != bb) return ba < bb;
		if (pieces[a].material != pieces[b].material) return pieces[a].material < pieces[b].material;
		return pieces[a].baseTex < pieces[b].baseTex;
	});

	g_sceneVB.Clear();
	g_batches.Clear();
	g_blendBatches.Clear();
	const void *cur = (const void *)(size_t)-1;
	const void *curBase = (const void *)(size_t)-1;
	int curBlend = -1;
	for (int i = 0; i < npieces; i++)
	{
		const zx::levelmesh::MeshPiece &p = pieces[order[i]];
		if (p.range.count == 0) continue;

		// A blended piece never merges with its neighbour: the translucent pass reorders batches per
		// frame, and two pieces sharing a batch could not then be separated. A batch also breaks on
		// baseTex, because the animation pass re-resolves the whole batch from that one pointer.
		if (p.material != cur || p.baseTex != curBase || p.blendMode != curBlend || p.blendMode != 0)
		{
			SceneBatch b;
			b.material = p.material;
			b.first = (unsigned int)g_sceneVB.Size();
			b.count = 0;
			b.masked = MaterialIsMasked(p.material);
			b.srb = NULL;   // filled once the batch list is final
			b.baseTex = p.baseTex;
			b.resolved = p.material;
			b.blend = (p.blendMode == 2) ? 2 : (p.blendMode != 0 ? 1 : 0);
			b.sortX = b.sortY = b.sortZ = 0.f;
			b.normX = p.normX; b.normY = p.normY; b.normZ = p.normZ;
			g_batches.Push(b);
			if (b.blend != 0) g_blendBatches.Push((int)g_batches.Size() - 1);
			cur = p.material;
			curBase = p.baseTex;
			curBlend = p.blendMode;
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
			// [rc4l] The BATCH index, which is the material index a ray hit needs to pick its own
			// texture. This slot held a dynamic light index and has been dead since the shader
			// started testing every light, so it costs nothing and saves a parallel per-triangle
			// table that would have to be kept in step with the batch list by hand.
			dv.lightIndex = (float)(g_batches.Size() - 1);
			dv.nx = p.normX; dv.ny = p.normY; dv.nz = p.normZ;
			g_sceneVB.Push(dv);
		}
		SceneBatch &nb = g_batches[g_batches.Size() - 1];
		nb.count += p.range.count;
		// The centroid the translucent pass sorts on. Averaged from the piece's own vertices rather
		// than taken from MeshPiece::sortX, which only the dynamic path fills in.
		if (nb.blend != 0 && p.range.count > 0)
		{
			float ax = 0.f, ay = 0.f, az = 0.f;
			for (unsigned int v = 0; v < p.range.count; v++)
			{
				const FFlatVertex &sv = src[p.range.offset + v];
				ax += sv.x; ay += sv.y; az += sv.z;
			}
			const float inv = 1.f / (float)p.range.count;
			nb.sortX = ax * inv; nb.sortY = ay * inv; nb.sortZ = az * inv;
		}
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
	// [rc4l] BIND_RAY_TRACING as well, when the device has it. A buffer handed to BuildBLAS as
	// geometry must declare that use at creation; without it the build appears to succeed and the
	// process dies on a later submit with nothing in the log. Vertex buffer first -- the raster path
	// still draws from this exact buffer.
	bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	if (RayTracingAvailable())
	{
		// Also readable as storage: a ray query returns a primitive index, so the shader has to fetch
		// the triangle's own vertices to shade it. One float per element keeps the GLSL side a plain
		// float array instead of matching a struct layout across two languages.
		bd.BindFlags |= Diligent::BIND_RAY_TRACING | Diligent::BIND_SHADER_RESOURCE;
		bd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
		bd.ElementByteStride = sizeof(float);
	}
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
	{
		Diligent::IPipelineState *pso =
			(g_batches[i].blend == 0) ? g_maskedPSO.RawPtr() :
			(g_batches[i].blend == 2) ? g_addPSO.RawPtr() : g_transPSO.RawPtr();
		g_batches[i].srb = GetMaterialSRB(pso, g_batches[i].material);
	}
	return true;
}
// See ReleaseMaterialSRBs. The batches keep raw copies of bindings they do not own.
static void ForgetBatchSRBs()
{
	for (unsigned i = 0; i < g_batches.Size(); i++) g_batches[i].srb = NULL;
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
	// [rc4l] The acceleration structure is built from the same buffer the raster path draws, right
	// after it is uploaded, so there is never a version of the level that one path can see and the
	// other cannot.
	// Only when something will actually trace against it: an acceleration structure over 17k
	// triangles is not free to build, and nothing else in the frame reads it.
	if (fua_dg_rtmirrors && RayTracingAvailable())
	{
		EnsureAccelerationStructure();
		// [rc4l] And the mirror bindings, here rather than mid-frame: creating them touches textures
		// and acceleration structures, and neither is valid inside a render pass.
		EnsureMirrorResources();
	}
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
	report.Format("uploaded %d verts (%.2f MB), %d pieces -> %d material batches (%d translucent) "
		"[lightmode %d, fogmode %d, %d%% soft-lit, %d%% fogged]",
		g_sceneVerts, (double)g_sceneVB.Size() * sizeof(SceneVertex) / (1024.0 * 1024.0),
		npieces, (int)g_batches.Size(), (int)g_blendBatches.Size(),
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
	if (!g_vb || g_sceneVB.Size() == 0) return;

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
		const float radius = light->GetRadius( ) * gl_lights_size;
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
		n++;
	}
	Printf( "fua_dg_lights: %d active, player floor %.1f\n", n, floorz );
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

// [rc4l] Everything that draws the WORLD, with the render target already bound and cleared.
//
// Split out of DrawSceneOnce so the same code can draw into something other than the screen. A camera
// texture is the same world from a different viewpoint, so it wants exactly this and none of the
// screen-only parts -- no 2D layer, no present, no window pump. Portals and mirrors will want the
// same seam.

// [rc4l] Invert a 4x4. Needed once a frame to turn a depth sample back into a world position.
bool InvertMatrix4(const float *m, float *out)
{
	float inv[16];
	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

	float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if (det > -1e-12f && det < 1e-12f) return false;
	det = 1.0f / det;
	for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
	return true;
}

static void DrawWorld(Diligent::IDeviceContext *ctx);

// [rc4l] The decal pass, in dgdecals.cpp. Runs between the world and the sprites.
void DrawDeferredDecals(Diligent::IDeviceContext *ctx);

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
static bool g_asBuilt = false;
static int  g_asVerts = 0;

static bool RayTracingAvailable()
{
	auto *dev = GetDevice();
	return dev != NULL && dev->GetDeviceInfo().Features.RayTracing;
}

void ReleaseAccelerationStructures()
{
	g_tlas.Release();
	g_blas.Release();
	g_asScratch.Release();
	g_tlasInstances.Release();
	g_asBuilt = false;
	g_asVerts = 0;
	ReleaseMirrorBinding();   // the mirror SRB holds a reference to the TLAS
}

// Build (or rebuild) the level's acceleration structure from the scene vertex buffer.
static bool EnsureAccelerationStructure()
{
	if (g_asBuilt && g_asVerts == (int)g_sceneVB.Size()) return true;
	if (!RayTracingAvailable() || !g_vb || g_sceneVB.Size() < 3) return false;

	auto *dev = GetDevice();
	auto *ctx = GetContext();
	ReleaseAccelerationStructures();

	const Diligent::Uint32 vertCount = (Diligent::Uint32)g_sceneVB.Size();
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
	triData.pVertexBuffer        = g_vb;
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

	Diligent::TopLevelASDesc tlasDesc;
	tlasDesc.Name             = "fua level TLAS";
	tlasDesc.MaxInstanceCount = 1;
	tlasDesc.Flags            = Diligent::RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
	dev->CreateTLAS(tlasDesc, &g_tlas);
	if (!g_tlas) return false;

	Diligent::BufferDesc ib;
	ib.Name      = "fua TLAS instances";
	ib.Usage     = Diligent::USAGE_DEFAULT;
	ib.BindFlags = Diligent::BIND_RAY_TRACING;
	ib.Size      = Diligent::TLAS_INSTANCE_DATA_SIZE;
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

	Diligent::TLASBuildInstanceData inst;
	inst.InstanceName = "level";
	inst.pBLAS        = g_blas;
	inst.Mask         = 0xFF;

	Diligent::BuildTLASAttribs tlasAttribs;
	tlasAttribs.pTLAS                        = g_tlas;
	tlasAttribs.pInstances                   = &inst;
	tlasAttribs.InstanceCount                = 1;
	tlasAttribs.pInstanceBuffer              = g_tlasInstances;
	tlasAttribs.pScratchBuffer               = g_asScratch;
	tlasAttribs.TLASTransitionMode           = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.BLASTransitionMode           = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.InstanceBufferTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	tlasAttribs.ScratchBufferTransitionMode  = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->BuildTLAS(tlasAttribs);

	g_asBuilt = true;
	g_asVerts = (int)g_sceneVB.Size();
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
	float v[6][6];        // two triangles: position then normal, mesh space (x, z-up, y)
	float nx, ny, d;      // plane in MAP space (nx*x + ny*y = d), for reflecting the camera
};
static TArray<MirrorSurface> g_mirrors;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_mirrorVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_mirrorPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_mirrorSRB;
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_mirrorColor, g_mirrorDepth;
static int g_mirrorW = 0, g_mirrorH = 0;
// True once the TLAS is genuinely bound to the mirror shader; false means use the planar path.
static bool g_mirrorTraced = false;

static void CollectMirrors()
{
	g_mirrors.Clear();
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
		const float zf1 = FIXED2FLOAT(ln->frontsector->floorplane.ZatPoint(ln->v1->x, ln->v1->y));
		const float zc1 = FIXED2FLOAT(ln->frontsector->ceilingplane.ZatPoint(ln->v1->x, ln->v1->y));
		const float zf2 = FIXED2FLOAT(ln->frontsector->floorplane.ZatPoint(ln->v2->x, ln->v2->y));
		const float zc2 = FIXED2FLOAT(ln->frontsector->ceilingplane.ZatPoint(ln->v2->x, ln->v2->y));

		const float dx = x2 - x1, dy = y2 - y1;
		const float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.001f) continue;

		MirrorSurface m;
		// Mesh space is (x, z-up, y), the same swap the level mesh uses. Six floats per vertex:
		// position, then the surface normal the ray query reflects the eye ray about.
		const float a[3] = { x1, zf1, y1 }, b[3] = { x1, zc1, y1 };
		const float c[3] = { x2, zf2, y2 }, e[3] = { x2, zc2, y2 };
		const float *tri[6] = { a, b, c, c, b, e };
		const float mnx = dy / len, mnz = -dx / len;
		for (int k = 0; k < 6; k++)
		{
			for (int q = 0; q < 3; q++) m.v[k][q] = tri[k][q];
			m.v[k][3] = mnx; m.v[k][4] = 0.f; m.v[k][5] = mnz;
		}

		m.nx = dy / len; m.ny = -dx / len;
		m.d = m.nx * x1 + m.ny * y1;
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

static const char *kMirrorVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec3 aNormal;\n"
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	"layout(location = 0) out vec3 vWorld;\n"
	"layout(location = 1) out vec3 vNormal;\n"
	"void main() {\n"
	"    vWorld = aPos;\n"
	"    vNormal = aNormal;\n"
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
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; };\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(binding = 2) uniform accelerationStructureEXT uTLAS;\n"
	"layout(std430, binding = 3) readonly buffer Verts { float vtx[]; };\n"
	"layout(binding = 4) uniform sampler2D uMaterials[128];\n"
	"layout(binding = 5) uniform sampler2D uSky;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"const uint STRIDE = 19u;\n"
	"vec3 vertColor(uint v) { uint b = v * STRIDE + 5u; return vec3(vtx[b], vtx[b + 1u], vtx[b + 2u]); }\n"
	"vec2 vertUV(uint v)    { uint b = v * STRIDE + 3u; return vec2(vtx[b], vtx[b + 1u]); }\n"
	"void main() {\n"
	"    vec3 n = normalize(vNormal);\n"
	"    vec3 eye = normalize(vWorld - uCameraPos.xyz);\n"
	"    vec3 dir = reflect(eye, n);\n"
	"    rayQueryEXT rq;\n"
	// Nudged off the surface, or the mirror hits itself at t = 0.
	"    rayQueryInitializeEXT(rq, uTLAS, gl_RayFlagsOpaqueEXT, 0xFF, vWorld + n * 0.5, 0.1, dir, 20000.0);\n"
	"    while (rayQueryProceedEXT(rq)) {}\n"
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
	"        outColor = vec4(texture(uSky, vec2(su * uScreen.z, sv * uScreen.w)).rgb, 1.0);\n"
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
	"    uint mat = uint(vtx[i0 * STRIDE + 15u] + 0.5) & 127u;\n"
	"    c *= texture(uMaterials[nonuniformEXT(mat)], uv).rgb;\n"
	// Distance falls off the same way the raster path's fog does, so a far reflection reads as far.
	"    float t = rayQueryGetIntersectionTEXT(rq, true);\n"
	"    c *= clamp(1.0 - t / 4000.0, 0.15, 1.0);\n"
	"    outColor = vec4(c, 1.0);\n"
	"}\n";

static bool EnsureMirrorResources()
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;
	const int w = (int)swap->GetDesc().Width, h = (int)swap->GetDesc().Height;

	if (!g_mirrorColor || g_mirrorW != w || g_mirrorH != h)
	{
		g_mirrorColor.Release(); g_mirrorDepth.Release(); g_mirrorSRB.Release();
		Diligent::TextureDesc td;
		td.Name = "fua mirror colour";
		td.Type = Diligent::RESOURCE_DIM_TEX_2D;
		td.Width = (Diligent::Uint32)w; td.Height = (Diligent::Uint32)h;
		td.Format = swap->GetDesc().ColorBufferFormat;
		td.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
		dev->CreateTexture(td, nullptr, &g_mirrorColor);
		Diligent::TextureDesc dd = td;
		dd.Name = "fua mirror depth";
		dd.Format = swap->GetDesc().DepthBufferFormat;
		dd.BindFlags = Diligent::BIND_DEPTH_STENCIL;
		dev->CreateTexture(dd, nullptr, &g_mirrorDepth);
		if (!g_mirrorColor || !g_mirrorDepth) return false;
		g_mirrorW = w; g_mirrorH = h;
	}

	if (!g_mirrorPSO)
	{
		Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
		{
			Diligent::ShaderCreateInfo ci;
			ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
			ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
			ci.Desc.Name = "fua mirror VS"; ci.Source = kMirrorVS;
			dev->CreateShader(ci, &vs);
		}
		{
			Diligent::ShaderCreateInfo ci;
			ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
			ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
			ci.Desc.Name = "fua mirror PS"; ci.Source = kMirrorPS;
			dev->CreateShader(ci, &ps);
		}
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
		};
		static Diligent::SamplerDesc samp;
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
		static Diligent::SamplerDesc matSamp = samp;
		matSamp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
		matSamp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;

		static Diligent::ImmutableSamplerDesc samplers[] = {
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
		if (auto *v = g_mirrorPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants"))
			v->Set(g_cb);
		if (auto *v = g_mirrorPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants"))
			v->Set(g_cb);
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
			for (Diligent::Uint32 mi = 0; mi < 128; mi++)   // matches uMaterials[128] in the shader
			{
				const void *mat = (mi < g_batches.Size()) ? g_batches[mi].resolved : NULL;
				Diligent::IDeviceObject *obj = GetMaterialSRV(mat, 0);
				if (obj == nullptr) obj = white;
				if (mi < g_batches.Size() && obj == white) fellBack++;
				v->SetArray(&obj, mi, 1);
			}
			// [rc4l] Count how many came back as the white placeholder. A reflection showing flat white
			// surfaces looks the same whether the texture failed to upload or the index is wrong, and
			// this separates the two without reading it off a screenshot.
			Printf("mirror: %u materials bound, %u fell back to white\n",
				(unsigned)g_batches.Size(), fellBack);
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
		// [rc4l] InitStaticResources = false. With true, Diligent copies the pipeline's static resources
		// into the binding at creation, and a static ARRAY with any element left unbound takes the
		// process down rather than reporting it.
		g_mirrorPSO->CreateShaderResourceBinding(&g_mirrorSRB, false);
		if (!g_mirrorSRB) return false;
		// Static resources are copied in explicitly, so a failure here is reported rather than being a
		// silent crash inside CreateShaderResourceBinding.
		g_mirrorPSO->InitializeStaticSRBResources(g_mirrorSRB);
		if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(g_mirrorColor->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
		// The traced path is only taken if the binding actually took. A variable the shader does not
		// declare, or a structure that failed to build, means falling back rather than committing an
		// SRB with a hole in it.
		g_mirrorTraced = false;
		if (g_tlas && fua_dg_rtmirrors)
		{
			if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTLAS"))
			{
				v->Set(g_tlas);
				g_mirrorTraced = true;
			}
			if (auto *v = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "Verts"))
			{
				v->Set(g_vb->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
			}
			// [rc4l] The sky, resolved here rather than reused from EnsureSky: that runs mid-frame with
			// a render pass open, and this may have to upload the texture.
			if (auto *v2 = g_mirrorSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSky"))
			{
				FTexture *rawSky = sky1texture.isValid() ? TexMan(sky1texture) : NULL;
				FMaterial *skyMat = rawSky ? FMaterial::ValidateTexture(rawSky, false) : NULL;
				Diligent::IDeviceObject *srv = GetMaterialSRV(skyMat, 0);
				v2->Set(srv ? srv : GetMaterialSRV(NULL, 0));
			}

		}
	}

	if (!g_mirrorVB && g_mirrors.Size() > 0)
	{
		TArray<float> verts;
		for (unsigned i = 0; i < g_mirrors.Size(); i++)
			for (int k = 0; k < 6; k++)
				for (int q = 0; q < 6; q++) verts.Push(g_mirrors[i].v[k][q]);
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

// Draw one mirror's surface. Shared by both paths: with ray tracing the shader traces from here, and
// without it the surface samples the reflection rendered a moment earlier.
static void ReleaseMirrorBinding() { g_mirrorSRB.Release(); g_mirrorTraced = false; }

static void DrawMirrorSurface(Diligent::IDeviceContext *ctx, unsigned index)
{
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
		cb[16] = FIXED2FLOAT(viewx); cb[17] = FIXED2FLOAT(viewz);
		cb[18] = FIXED2FLOAT(viewy); cb[19] = (float)(int)fua_dg_lightmode;
		cb[20] = (float)g_lightCount; cb[21] = g_skyAngle; cb[22] = 0.f; cb[23] = 0.f;
		for (int k = 0; k < 4; k++) cb[24 + k] = 0.f;
		cb[28] = (float)g_mirrorW; cb[29] = (float)g_mirrorH; cb[30] = g_skyXScale; cb[31] = g_skyVScale;
		cb[32] = g_skyCapColor[0].r / 255.f;
		cb[33] = g_skyCapColor[0].g / 255.f;
		cb[34] = g_skyCapColor[0].b / 255.f;
		cb[35] = 0.f;
	}
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
	ctx->CommitShaderResources(g_mirrorSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs d;
	d.NumVertices = 6;
	d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(d);
}

// Render each mirror's reflection and draw its surface. Called after the world, before the 2D layer.
static void RenderMirrors(Diligent::IDeviceContext *ctx)
{
	if (g_mirrors.Size() == 0) return;
	if (!g_mirrorPSO || !g_mirrorSRB || !g_mirrorVB)
	{
		if (!EnsureMirrorResources()) return;
	}

	auto *swap = GetSwapChain();
	auto *brtv = swap->GetCurrentBackBufferRTV();
	// [rc4l] The world pass renders into our own readable depth buffer, so restoring the swapchain's
	// here would hand the screen a depth buffer nothing has written to.
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = swap->GetDepthBufferDSV();
	Diligent::ITextureView *mrtv = g_mirrorColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	Diligent::ITextureView *mdsv = g_mirrorDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);

	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	float savedMVP[16];
	for (int i = 0; i < 16; i++) savedMVP[i] = g_mvp[i];

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

	for (unsigned i = 0; i < g_mirrors.Size(); i++)
	{
		const MirrorSurface &m = g_mirrors[i];
		if (traced)
		{
			DrawMirrorSurface(ctx, i);
			continue;
		}

		// Reflect the camera through the mirror plane. Position mirrors across the plane; the facing
		// reflects as 2*mirrorAngle - viewangle, which is the same rotation expressed in Doom's
		// integer angles rather than as a matrix.
		const float px = FIXED2FLOAT(sx), py = FIXED2FLOAT(sy);
		const float dist = m.nx * px + m.ny * py - m.d;
		const float rx = px - 2.f * dist * m.nx;
		const float ry = py - 2.f * dist * m.ny;

		// The line's own direction. n = (dy, -dx)/len, so the direction is (-ny, nx) -- getting this
		// backwards is invisible on an axis-aligned mirror, where the two answers differ by exactly
		// 360 degrees, and wrong on every other one.
		const float mirrorAng = atan2f(m.nx, -m.ny);
		const float camAng = (float)(sa >> ANGLETOFINESHIFT) * 2.f * 3.14159265f / 8192.f;
		const float TWOPI = 2.f * 3.14159265f;

		// [rc4l] Wrap before converting. 2*mirrorAngle - camAngle is negative for a large share of
		// camera angles, and converting a negative double to angle_t -- an UNSIGNED type -- is
		// undefined. It came out as a direction unrelated to the reflection and lurching as the player
		// turned, which reads as the mirror tracking the camera rather than reflecting it.
		float refAng = 2.f * mirrorAng - camAng;
		refAng = fmodf(refAng, TWOPI);
		if (refAng < 0.f) refAng += TWOPI;

		viewx = FLOAT2FIXED(rx); viewy = FLOAT2FIXED(ry); viewz = sz;
		viewangle = (angle_t)(refAng / TWOPI * 4294967296.0);
		BuildMVP(g_mvp);
		viewx = sx; viewy = sy; viewz = sz; viewangle = sa;

		// Clip to the mirror's own plane, in MESH space: x and z there are the map's x and y.
		g_clipPlane[0] = m.nx; g_clipPlane[1] = 0.f; g_clipPlane[2] = m.ny;
		g_clipPlane[3] = -m.d;
		if (g_clipPlane[3] == 0.f) g_clipPlane[3] = 0.0001f;   // w == 0 means "off"

		ctx->SetRenderTargets(1, &mrtv, mdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
		ctx->ClearRenderTarget(mrtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		ctx->ClearDepthStencil(mdsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		DrawWorld(ctx);

		// Back to the screen, with the real camera, and draw this mirror's surface.
		g_clipPlane[0] = g_clipPlane[1] = g_clipPlane[2] = g_clipPlane[3] = 0.f;
		for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
		ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		{
			Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
			for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
			cb[16] = FIXED2FLOAT(viewx); cb[17] = FIXED2FLOAT(viewz);
			cb[18] = FIXED2FLOAT(viewy); cb[19] = (float)(int)fua_dg_lightmode;
			cb[20] = (float)g_lightCount; cb[21] = g_skyAngle; cb[22] = 0.f; cb[23] = 0.f;
			for (int k = 0; k < 4; k++) cb[24 + k] = 0.f;
			cb[28] = (float)g_mirrorW; cb[29] = (float)g_mirrorH; cb[30] = g_skyXScale; cb[31] = g_skyVScale;
		cb[32] = g_skyCapColor[0].r / 255.f;
		cb[33] = g_skyCapColor[0].g / 255.f;
		cb[34] = g_skyCapColor[0].b / 255.f;
		cb[35] = 0.f;
		}
		Diligent::IBuffer *vbs[] = { g_mirrorVB };
		const Diligent::Uint64 offsets[] = { (Diligent::Uint64)i * 36 * sizeof(float) };
		ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
			Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->SetPipelineState(g_mirrorPSO);
		ctx->CommitShaderResources(g_mirrorSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::DrawAttribs d;
		d.NumVertices = 6;
		d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(d);
	}

	viewx = sx; viewy = sy; viewz = sz; viewangle = sa;
	for (int i = 0; i < 16; i++) g_mvp[i] = savedMVP[i];
}

// [rc4l] Keep the readable depth buffer matched to the swapchain, recreating it when the size
// changes. Returns NULL if it cannot be made, and every caller then falls back to the swapchain's
// own depth buffer -- a missing screen-space effect is a better failure than a black screen.
static Diligent::ITextureView *EnsureSceneDepth()
{
	auto *swap = GetSwapChain();
	auto *dev = GetDevice();
	if (!swap || !dev) return NULL;
	const auto &sd = swap->GetDesc();
	if (g_sceneDepth && g_sceneDepthW == (int)sd.Width && g_sceneDepthH == (int)sd.Height)
		return g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);

	// [rc4l] Every cached binding of the OLD depth texture goes with it.
	//
	// The decal pass samples this texture through a shader resource binding that is cached per
	// material and reused across frames. Resizing the window recreates the texture underneath those
	// bindings, and whatever they then resolve to is not the depth of the frame being drawn -- which
	// came out as a grid of squares across every mark, since the reconstruction is reading positions
	// that never existed.
	g_sceneDepth.Release();
	g_sceneNormal.Release();
	ReleaseMaterialSRBs();
	Diligent::TextureDesc td;
	td.Name = "fua scene depth";
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = sd.Width; td.Height = sd.Height;
	td.MipLevels = 1;
	td.Format = sd.DepthBufferFormat;
	td.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
	dev->CreateTexture(td, nullptr, &g_sceneDepth);
	if (!g_sceneDepth) { g_sceneDepthW = g_sceneDepthH = 0; return NULL; }
	{
		// The G-buffer shares the depth's lifetime and size: they are read together, one fragment at
		// a time, and a mismatch between them would be a reconstruction against the wrong surface.
		Diligent::TextureDesc nd;
		nd.Name = "fua scene normal";
		nd.Type = Diligent::RESOURCE_DIM_TEX_2D;
		nd.Width = sd.Width; nd.Height = sd.Height;
		nd.MipLevels = 1;
		nd.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
		nd.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
		dev->CreateTexture(nd, nullptr, &g_sceneNormal);
	}
	g_sceneDepthW = (int)sd.Width; g_sceneDepthH = (int)sd.Height;
	return g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
}

static Diligent::ITextureView *SceneNormalRTV()
{
	return g_sceneNormal ? g_sceneNormal->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET) : NULL;
}

Diligent::ITextureView *SceneNormalSRV()
{
	return g_sceneNormal ? g_sceneNormal->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : NULL;
}

Diligent::ITextureView *SceneDepthDSV()
{
	return g_sceneDepth ? g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL) : NULL;
}

Diligent::ITextureView *SceneDepthSRV()
{
	return g_sceneDepth ? g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE) : NULL;
}

#ifdef ZX_ENABLE_REPLAY
// Defined below, beside the readback machinery it owns.
static void CaptureReplayFrame(Diligent::IDeviceContext *ctx, Diligent::ISwapChain *swap);
#endif

static void DrawSceneOnce(bool present = true, bool pump = true)
{
	auto *ctx = GetContext();
	auto *swap = GetSwapChain();

	// [rc4l] A minimised window has a zero-sized swapchain, and there is nothing to draw into it.
	// Carrying on means asking the device for 0x0 textures, which fails, and then rendering against
	// the null views that come back.
	if (!ctx || !swap) return;
	{
		const auto &sd = swap->GetDesc();
		if (sd.Width == 0 || sd.Height == 0) return;
	}

	auto *rtv = swap->GetCurrentBackBufferRTV();
	Diligent::ITextureView *dsv = EnsureSceneDepth();
	if (!dsv) dsv = swap->GetDepthBufferDSV();
	// [rc4l] Colour and the G-buffer together, for the opaque world only. DrawWorld drops back to
	// colour alone before the decals, which read the G-buffer rather than write it.
	Diligent::ITextureView *nrm = SceneNormalRTV();
	g_gbufBound = (nrm != NULL);
	Diligent::ITextureView *targets[2] = { rtv, nrm };
	ctx->SetRenderTargets(g_gbufBound ? 2 : 1, targets,
		dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	if (g_gbufBound)
	{
		// Zero means "nothing drew here", which decodes to a zero-length normal -- and the decal pass
		// treats that as a surface it knows nothing about rather than one facing a particular way.
		const float clearN[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
		ctx->ClearRenderTarget(nrm, clearN, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	ctx->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	DrawWorld(ctx);
	RenderMirrors(ctx);

	// [rc4l] 2D last, over everything, with depth off entirely -- it is the frame's top layer.
	Draw2D(ctx);

#ifdef ZX_ENABLE_REPLAY
	// [rc4l] Instant replay: hand this frame to the Vulkan recorder stream before presenting.
	//
	// Present flips to the next swapchain image, after which the current back buffer's contents are
	// undefined -- reading it afterwards gave a convincing all-black PNG the first time
	// SceneScreenshot did it. So the copy has to happen here.
	if (present) CaptureReplayFrame(ctx, swap);
#endif

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

// [rc4l] Camera textures: the world, rendered from somewhere else, into a texture.
//
// A camera texture has no pixels of its own -- the engine renders into it every frame -- so reading
// its bytes returns whatever the buffer happens to hold. That was the red/blue noise on gvh06's
// teleporter and the flat slab on gvh07's monitor: the same bug wearing two faces.
//
// Nothing here is specific to cameras. It is the world drawn with a different view matrix into a
// different target, which is also what a portal, a mirror and a skybox each need, so this is the
// seam those get built on rather than a one-off.
struct CameraTarget
{
	const void *material;
	int w, h;
	Diligent::RefCntAutoPtr<Diligent::ITexture> color, depth;
	Diligent::ITextureView *rtv, *dsv, *srv;
};
static TArray<CameraTarget *> g_cameraTargets;

static CameraTarget *GetCameraTarget(const void *material, int w, int h)
{
	for (unsigned i = 0; i < g_cameraTargets.Size(); i++)
	{
		CameraTarget *c = g_cameraTargets[i];
		if (c->material == material && c->w == w && c->h == h) return c;
	}
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || w <= 0 || h <= 0) return NULL;

	CameraTarget *c = new CameraTarget();
	c->material = material; c->w = w; c->h = h;
	c->rtv = c->dsv = c->srv = NULL;

	Diligent::TextureDesc td;
	td.Name = "fua camera colour";
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = (Diligent::Uint32)w; td.Height = (Diligent::Uint32)h;
	td.Format = swap->GetDesc().ColorBufferFormat;
	td.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
	dev->CreateTexture(td, nullptr, &c->color);

	Diligent::TextureDesc dd = td;
	dd.Name = "fua camera depth";
	dd.Format = swap->GetDesc().DepthBufferFormat;
	dd.BindFlags = Diligent::BIND_DEPTH_STENCIL;
	dev->CreateTexture(dd, nullptr, &c->depth);

	if (!c->color || !c->depth) { delete c; return NULL; }
	c->rtv = c->color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	c->srv = c->color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
	c->dsv = c->depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	g_cameraTargets.Push(c);
	return c;
}

// The rendered view for a canvas material, or NULL if it has never been rendered.
Diligent::ITextureView *GetCameraSRV(const void *material)
{
	for (unsigned i = 0; i < g_cameraTargets.Size(); i++)
		if (g_cameraTargets[i]->material == material) return g_cameraTargets[i]->srv;
	return NULL;
}

// [rc4l] Render one camera texture. Called from FGLInterface::RenderTextureView, once per visible
// camera per frame, with the viewpoint the engine picked.
//
// The viewpoint arrives by temporarily standing the global camera somewhere else. BuildMVP reads
// viewx/viewy/viewz/viewangle/viewpitch because every other caller wants the player's view; swapping
// them around this call is smaller and less error-prone than threading a viewpoint through the
// matrix builder and every one of its callers, and they are restored before anything else can look.
void RenderCameraTexture(const void *material, int w, int h,
                         int px, int py, int pz, unsigned int pangle, int ppitch, float fovDeg)
{
	if (!GetDevice() || !g_vb || !g_scenePSO) return;
	CameraTarget *cam = GetCameraTarget(material, w, h);
	if (!cam) return;

	auto *ctx = GetContext();

	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	const int sp = viewpitch;
	viewx = px; viewy = py; viewz = pz; viewangle = pangle; viewpitch = ppitch;
	g_fovOverride = fovDeg * 3.14159265f / 180.0f;
	g_aspectOverride = (h > 0) ? (float)w / (float)h : 1.f;
	BuildMVP(g_mvp);
	g_fovOverride = g_aspectOverride = 0.f;
	viewx = sx; viewy = sy; viewz = sz; viewangle = sa; viewpitch = sp;

	Diligent::ITextureView *rtv = cam->rtv;
	ctx->SetRenderTargets(1, &rtv, cam->dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::Viewport vp;
	vp.TopLeftX = 0; vp.TopLeftY = 0;
	vp.Width = (float)w; vp.Height = (float)h;
	vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
	ctx->SetViewports(1, &vp, (Diligent::Uint32)w, (Diligent::Uint32)h);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(cam->dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	DrawWorld(ctx);

	// Hand the screen back its own target and full viewport; the main pass assumes both.
	auto *swap = GetSwapChain();
	auto *brtv = swap->GetCurrentBackBufferRTV();
	// [rc4l] The world pass renders into our own readable depth buffer, so restoring the swapchain's
	// here would hand the screen a depth buffer nothing has written to.
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = swap->GetDepthBufferDSV();
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	BuildMVP(g_mvp);
}

void ReleaseCameraTargets()
{
	for (unsigned i = 0; i < g_cameraTargets.Size(); i++) delete g_cameraTargets[i];
	g_cameraTargets.Clear();
}

// [rc4l] Read-only handles for passes that live in their own file. See dgshared.h.
Diligent::IBuffer *SceneConstantsCB() { return g_cb; }
const float *SceneMVP() { return g_mvp; }

static void DrawWorld(Diligent::IDeviceContext *ctx)
{
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
		cb[20] = (float)g_lightCount;
		// [rc4l] The sky rotation for this frame: RenderDome's -180 degrees plus the scroll.
		cb[21] = g_skyAngle;
		cb[22] = 0.f; cb[23] = 0.f;
		// [rc4l] uClipPlane: while a mirror's reflection renders, everything on the FAR side of the
		// mirror -- the wall it hangs on and the rooms behind it -- sits between the reflected camera
		// and the scene and would occlude the entire reflection. w == 0 disables it, which is every
		// ordinary pass.
		for (int ci = 0; ci < 4; ci++) cb[24 + ci] = g_clipPlane[ci];
		cb[28] = (float)GetSwapChain()->GetDesc().Width;
		cb[29] = (float)GetSwapChain()->GetDesc().Height;
		cb[30] = g_skyXScale; cb[31] = g_skyVScale;
		// [rc4l] What a ray sees when it hits nothing. Sky ceilings are portals and are deliberately
		// not in the mesh, so a reflection looking upward hits nothing -- and painting that black put
		// holes in the reflected floor and sky. The sky's own cap colour is the honest answer.
		cb[32] = g_skyCapColor[0].r / 255.f;
		cb[33] = g_skyCapColor[0].g / 255.f;
		cb[34] = g_skyCapColor[0].b / 255.f;
		cb[35] = 0.f;
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
			Diligent::IPipelineState *pso =
				(b.blend == 0) ? g_maskedPSO.RawPtr() :
				(b.blend == 2) ? g_addPSO.RawPtr() : g_transPSO.RawPtr();
			if (auto *srb = GetMaterialSRB(pso, now)) b.srb = srb;
		}
	}

	// [rc4l] Sky first, with depth off, so the world paints over whatever it does not cover.
	if (fua_dg_sky)
	{
		// mSky1Pos is in degrees and advances with gl_frameMS, so it must be read every frame.
		const float scroll = (GLRenderer != NULL) ? GLRenderer->mSky1Pos : 0.f;
		g_skyAngle = (-180.0f + scroll) * 3.14159265f / 180.0f;
		EnsureSky();
		DrawSky(ctx);
	}

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
	ctx->SetPipelineState(g_gbufBound ? g_maskedGBufPSO : g_maskedPSO);

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
		SceneBatch &b = g_batches[bi];
		if (b.count == 0 || b.blend != 0) continue;
		// Re-resolve rather than skip: a binding dropped by a resize would otherwise leave that
		// material missing from the world until the next upload.
		if (b.srb == NULL) b.srb = GetMaterialSRB(g_maskedPSO.RawPtr(), b.material);
		if (b.srb == NULL) continue;

		ctx->CommitShaderResources(b.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		Diligent::DrawAttribs draw;
		draw.NumVertices = b.count;
		draw.StartVertexLocation = b.first;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);
	}

	// [rc4l] The G-buffer is finished with: everything from here reads it or ignores it, and every
	// pipeline after this point declares a single attachment.
	if (g_gbufBound)
	{
		auto *swapNow = GetSwapChain();
		Diligent::ITextureView *rtvNow = swapNow ? swapNow->GetCurrentBackBufferRTV() : NULL;
		if (rtvNow) ctx->SetRenderTargets(1, &rtvNow, SceneDepthDSV(),
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		g_gbufBound = false;
	}

	// [rc4l] Decals mark surfaces, so they go with the surfaces -- after the world and before the
	// sprites. Anything standing in front of a mark is then drawn over it because it is in a LATER
	// pass, which is a fact about the frame rather than something a sort has to rediscover.
	if (zx::levelmesh::ProjectedDecalsActive()) DrawDeferredDecals(ctx);

	// [rc4l] Sprites are built here but ALL of them draw in the sorted pass below, never in an
	// opaque one. See DrawBlended.
	BuildDynamic(ctx);

	// [rc4l] Then everything that blends -- translucent 3D floors, translucent middle textures and
	// translucent sprites -- in ONE back-to-front pass, so the world and the sprites sort against each
	// other rather than being layered by which pass came last.
	DrawBlended(ctx);
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
void ReleaseCameraTargets();

void ReleaseAccelerationStructures();
static void CollectMirrors();

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
		// [rc4l] Drop everything keyed on a pointer the old level owned.
		//
		// The texture cache and the material SRBs are keyed on raw FMaterial*, and ReleaseMaterials
		// was never called from anywhere -- so they survived every map change for the life of the
		// process. That is fine until the engine frees a level's materials and the allocator hands
		// the same addresses to a new level's, at which point the cache serves the PREVIOUS map's
		// texture for a material that merely happens to live where the old one did. It needs several
		// map changes to show up, which is exactly how it was found: a map that renders correctly on
		// its own came up with black and wrong surfaces after cycling through the rotation to reach
		// it. SRBs reference the textures, so they go first.
		ReleaseBatchSRBs();
		ReleaseMaterialSRBs();
		for (unsigned int b = 0; b < g_batches.Size(); b++) g_batches[b].srb = NULL;
		ReleaseMaterials();
		// Camera targets are keyed on FMaterial* too, and go stale for exactly the same reason.
		ReleaseCameraTargets();
		ReleaseAccelerationStructures();
		CollectMirrors();
		g_skyMaterial = NULL;
		g_skyBuiltValid = false;
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

// [rc4l] Read by dgwin32.cpp when it creates the window; see fua_dg_embed.
bool Fua_WantEmbeddedWindow() { return !!fua_dg_embed; }

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
#ifdef ZX_ENABLE_REPLAY
// [rc4l] Per-frame swapchain readback for the instant replay, double-buffered.
//
// Two staging textures, alternating: this frame is copied into one while the OTHER -- copied a frame
// ago and therefore long since complete -- is mapped and read. That is the same trick the GL path
// plays with its pair of PBOs, and for the same reason: mapping the texture you just wrote forces a
// WaitForIdle, which turns a 0.03 ms frame into a pipeline stall. SceneScreenshot can afford that
// because it happens once; thirty times a second it would be the most expensive thing in the frame.
//
// The first frame after startup reads a staging texture nothing has been copied into yet, so the
// pair is not consumed until both have been written once.
static Diligent::RefCntAutoPtr<Diligent::ITexture> g_replayStaging[2];
static int g_replayStagingW = 0, g_replayStagingH = 0;
static int g_replayIndex = 0;
static bool g_replayPrimed = false;

static void ReleaseReplayStaging()
{
	g_replayStaging[0].Release();
	g_replayStaging[1].Release();
	g_replayStagingW = g_replayStagingH = 0;
	g_replayIndex = 0;
	g_replayPrimed = false;
}

static void CaptureReplayFrame(Diligent::IDeviceContext *ctx, Diligent::ISwapChain *swap)
{
	if (!zx::replay::WantsFrameVulkan()) return;

	auto *dev = GetDevice();
	if (!dev || !ctx || !swap) return;
	auto *backTex = swap->GetCurrentBackBufferRTV()->GetTexture();
	if (!backTex) return;
	const auto &bd = backTex->GetDesc();

	if (!g_replayStaging[0] || g_replayStagingW != (int)bd.Width || g_replayStagingH != (int)bd.Height)
	{
		ReleaseReplayStaging();
		Diligent::TextureDesc sd;
		sd.Name = "fua replay readback";
		sd.Type = Diligent::RESOURCE_DIM_TEX_2D;
		sd.Width = bd.Width; sd.Height = bd.Height;
		sd.Format = bd.Format;
		sd.Usage = Diligent::USAGE_STAGING;
		sd.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
		sd.BindFlags = Diligent::BIND_NONE;
		sd.MipLevels = 1;
		for (int i = 0; i < 2; i++) dev->CreateTexture(sd, nullptr, &g_replayStaging[i]);
		if (!g_replayStaging[0] || !g_replayStaging[1]) { ReleaseReplayStaging(); return; }
		g_replayStagingW = (int)bd.Width; g_replayStagingH = (int)bd.Height;
	}

	const int write = g_replayIndex;
	const int read = g_replayIndex ^ 1;

	// [rc4l] Unbind the render targets before copying FROM the back buffer.
	//
	// Copying a texture that is still bound as a render target makes Diligent unbind it for you and
	// say so, every single frame -- 1719 pairs of info messages in one short session, 94% of the
	// engine log. This is the fix the message itself asks for, and doing it here costs nothing:
	// the capture runs immediately before Present, so nothing draws afterwards.
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

	Diligent::CopyTextureAttribs cta;
	cta.pSrcTexture = backTex;
	cta.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	cta.pDstTexture = g_replayStaging[write];
	cta.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->CopyTexture(cta);

	if (g_replayPrimed)
	{
		Diligent::MappedTextureSubresource mapped;
		// DO_NOT_WAIT: if last frame's copy is somehow still in flight, skip this frame rather than
		// stall. A dropped capture frame is invisible; a stall is not.
		ctx->MapTextureSubresource(g_replayStaging[read], 0, 0, Diligent::MAP_READ,
			Diligent::MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
		if (mapped.pData != nullptr)
		{
			const int w = g_replayStagingW, h = g_replayStagingH;
			// Channel order from the FORMAT, never assumed -- a hard-coded BGRA swap once created a
			// red/blue inversion that still looked like a plausible Doom scene, just cooler.
			const bool bgra = (bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM ||
			                   bd.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB);
			const int ri = bgra ? 2 : 0, bi = bgra ? 0 : 2;

			static TArray<unsigned char> rgb;
			rgb.Resize((unsigned)(w * h * 3));
			for (int y = 0; y < h; y++)
			{
				const unsigned char *srcRow = (const unsigned char *)mapped.pData + (size_t)y * mapped.Stride;
				unsigned char *dstRow = &rgb[0] + (size_t)y * w * 3;
				for (int x = 0; x < w; x++)
				{
					dstRow[x*3+0] = srcRow[x*4+ri];
					dstRow[x*3+1] = srcRow[x*4+1];
					dstRow[x*3+2] = srcRow[x*4+bi];
				}
			}
			ctx->UnmapTextureSubresource(g_replayStaging[read], 0, 0);
			// Already top-down, so a positive pitch.
			zx::replay::SubmitFrameVulkan(&rgb[0], w, h, w * 3);
		}
	}

	g_replayIndex = read;
	g_replayPrimed = true;
}
#endif // ZX_ENABLE_REPLAY

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

	// Same as the replay path: unbind before copying from the back buffer, or Diligent does it for
	// us and logs about it.
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);

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

// [rc4l] Re-scan the level for mirror lines, for when one has just been made.
//
// CollectMirrors runs when a level is armed and never again, which is correct for a map -- a
// linedef special does not change by itself. It does change when fua_make_mirror changes it, and
// without this the backend keeps drawing that wall as a solid one while GL is already reflecting
// in it. The traced reflection had no other way to be reached, which is most of why it went
// untested for so long.
CCMD( fua_dg_mirrors )
{
	Printf( "fua_dg_mirrors: %d mirror surface(s)\n", zx::hwrender::RecollectMirrors( ) );
}
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
