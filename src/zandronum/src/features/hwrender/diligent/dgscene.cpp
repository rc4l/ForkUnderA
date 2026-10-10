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
#include "features/surfaces/surfacebuild.h"
#include "features/levelmesh/wallcache.h"   // LevelGeneration, for the per-level auto setup
#include "features/levelmesh/levelmesh.h"   // ArmFullBake
#include "d_main.h"                          // gamestate
#include "features/levelmesh/flatmesh.h"
#include "features/horizon/horizon.h"   // [rc4l] the horizon lines' sky halves
#include "features/hwrender/computation/skyportal_compute.h"   // [rc4l] what the portal pass may draw
#include "features/levelmesh/computation/packedcolor_compute.h"
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] kSkyCapture*, the sky list a pass draws
#include "features/hwrender/computation/warp_compute.h"   // [rc4l] the warp texture ripple
#include "features/hwrender/computation/sceneconstants_compute.h"   // [rc4l] the constant block, packed in one place
#include "features/hwrender/computation/scenebinding_compute.h"
#include "features/hwrender/computation/planemirror_compute.h"   // [rc4l] reflective floors and ceilings   // [rc4l] a batch's pipeline, and when a resize rebuilds the targets
#include "features/hwrender/computation/growbuffer_compute.h"   // [rc4l] the per-frame vertex streams
#include "features/hwrender/computation/texfilter_compute.h"   // [rc4l] the engine's filter modes
#include "features/hwrender/computation/slotremap_compute.h"   // [rc4l] ANIMDEFS frames in the material array
#include "features/hwrender/computation/lighttouch_compute.h"   // [rc4l] GL's light links
#include "features/hwrender/computation/backendview_compute.h"   // [rc4l] the camera, as GL builds it
#include "features/hwrender/computation/sceneview_compute.h"   // [rc4l] GL's vertical field of view
#include "features/hwrender/diligent/dgshared.h"          // [rc4l] FUA_CONSTANTS_GLSL, the constant block every pass declares
#include "features/levelmesh/projdecals.h"   // [rc4l] the marks the decal pass draws
#include "features/hwrender/computation/surfaceorder_compute.h"   // the draw order, tested off-engine
#include "features/hwrender/hud2d.h"
#include "features/hwrender/diligent/dglight_glsl.h"   // [rc4l] the sector light, shared with the horizon pass
#include "v_video.h"
#include "gl/renderer/gl_renderer.h"
#include "gl/dynlights/gl_lightbuffer.h"
#include "features/hwrender/computation/lightcluster_compute.h"
#include "gl/dynlights/gl_dynlight.h"
#include "p_local.h"
#include "doomstat.h"
#include "templates.h"   // gl_lightdata.h's inline gl_ClampLight uses clamp<>
#include "gl/renderer/gl_lightdata.h"
#include "gl/data/gl_data.h"
#include "gl/scene/gl_portal.h"
#include "gl/textures/gl_material.h"
#include "r_sky.h"
#include "gl/textures/gl_skyboxtexture.h"   // FSkyBox: the six faces a skybox is
#include "g_shared/a_sharedglobal.h"        // ASkyViewpoint: the sky that is a place, not a picture
#include "features/fixed64/computation/angle_interp_compute.h"   // InterpolateAngleBAM
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
EXTERN_CVAR(Bool, gl_mirrors)

// [rc4l] 0 flat multiply (pre-lighting-port behaviour), 1 the ported equation, 2 depth as grey,
// 3 depth contours, 4 vertex colour only. Live per frame -- no rebake needed to switch.
CVAR(Int, fua_dg_lightmode, 1, CVAR_ARCHIVE)
// [rc4l] Dump the ORDER the translucent pass actually draws in, for one frame.
//
// Everything about a decal landing on the wrong side of a sprite is decided by one comparator, and
// nothing about the finished picture says what that comparator concluded -- "the scorch is over the
// glow" and "the glow is under the scorch" are the same screenshot. Two attempts at this were made
// by reading pictures and both were wrong. The list itself is one Printf away.
static bool g_dumpBlendOrder = false;
// [rc4l] Whether the world being drawn is the player's own view, the only one that dumps its order or paints plane reflections.
static bool g_drawingMainView = false;
// [rc4l] With a height given, the dump lists every draw whose span holds it instead of the nearest 40.
static bool g_dumpBlendAtZ = false;
static float g_dumpBlendZ = 0.f;
CCMD( fua_dg_blendorder )
{
	g_dumpBlendOrder = true;
	g_dumpBlendAtZ = argv.argc( ) > 1;
	g_dumpBlendZ = g_dumpBlendAtZ ? (float)atof( argv[1] ) : 0.f;
	Printf( "fua_dg_blendorder: dumping the next translucent pass\n" );
}
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
// Planar mirror reflections. On by default; 0 takes the whole pass out, for bisecting a GPU hang.
CVAR(Bool, fua_dg_mirrors_on, true, CVAR_ARCHIVE)
// [rc4l] How many reflective planes get a reflection a frame, each a render of the world; 0 turns them off.
CVAR(Int, fua_dg_planemirrors, 4, CVAR_ARCHIVE)
// [rc4l] How many stacked-sector views (GL's GLSectorStackPortal) get rendered a frame, each a render of the world; 0 turns them off.
CVAR(Int, fua_dg_stackportals, 2, CVAR_ARCHIVE)
// [rc4l] Shows the first stacked view's picture over the whole frame, to see what it holds beyond the planes it is painted on.
CVAR(Bool, fua_dg_stackshow, false, 0)
// The sky that is a place: render the world from an ASkyViewpoint and use it as the background.
CVAR(Bool, fua_dg_skyportal, true, CVAR_ARCHIVE)
// [rc4l] The portal's render scale as a DIVISOR of the swapchain: 1 is full, 2 is half. See
// EnsureSkyPortalResources for why full is the default.
CVAR(Int, fua_dg_skyportal_scale, 1, CVAR_ARCHIVE)

// [rc4l] How far from the portal camera the skybox room is allowed to reach, in map units. Zero
// disables the test and draws the whole level into the portal, which is what produced the
// rectangle. 2048 measured best of 2048/4096/8192 on wdiex02 -- 5.1 mean|d| against 7.0 for
// the wider two, which let map geometry back into the room.
CVAR(Float, fua_dg_skyportal_reach, 2048.f, CVAR_ARCHIVE)

// [rc4l] Re-resolve animated textures per frame. See the loop in DrawSceneOnce.
CVAR(Bool, fua_dg_animate, true, 0)

// [rc4l] Draw the captured 2D layer (HUD, menus, console) over the world.
CVAR(Bool, fua_dg_hud, true, 0)

// [rc4l] Dynamic lights: muzzle flashes, plasma, rocket trails, lamps.
CVAR(Bool, fua_dg_dynlights, true, 0)
// [rc4l] Light only what GL's light links reach (0 lights by radius alone, through walls), for an A/B.
CVAR(Bool, fua_dg_lightlinks, true, 0)

// [rc4l] Clustered lighting, and the switch that makes it falsifiable.
//
// With this off the fragment shader tests every light in the level, which is what it has always
// done; with it on it tests only the lights binned into its own cell. The two are supposed to draw
// the SAME PICTURE -- clustering is an acceleration, not a look -- so the switch is the test: flip
// it in a frozen frame and diff. Anything that shows up is a cell boundary disagreeing between the
// binning pass and the shader, which is the one failure mode this design has.
CVAR(Bool, fua_dg_clusters, true, 0)

// [rc4l] Cut the cord: the backend renders and GL stops deriving the scene.
//
// GL walks the BSP, builds a GLWall per visible seg and fills draw lists every frame; the backend
// renders from geometry that is already resident. On Sunder MAP16 that is ~7 ms of GL working out
// a picture the backend draws in 0.4. This switch stops GL doing it.
//
// ON by default now. The three things that kept it off are done: the level is baked when it loads,
// sectors that move are re-baked (InvalidateMovedSectors), and sprites are registered rather than
// rasterised twice. StandaloneActive() reports whether it is really carrying the frame, which is
// not the same as the cvar being set.
//
// What the switch is worth, Sunder MAP10's arena with monsters live: 12.1 ms a frame became 2.1,
// and the p99 16.8 ms became 4.9. What it costs in accuracy, measured against the GL-driven picture
// on dbab01..dbab05 with the world settled: four maps at 0.0%, and dbab04 at 0.5% -- one wall band
// that comes out grey where GL tints it, geometry and texture both already right. That band is the
// only known difference and it is written up in features/surfaces/README.md.
CVAR(Bool, fua_dg_standalone, true, CVAR_ARCHIVE)


// [rc4l] Bindless materials: every texture in the level reachable from one descriptor set.
//
// With this on, the world does not bind a material per batch. Each vertex already carries a material
// SLOT -- the field that used to hold a dynamic light index and has been dead since the shader
// started testing every light -- and the fragment shader picks its own texture out of an array.
//
// The point is not the array. It is that the per-material shader resource binding goes away, and
// with it the reason a batch has to span the level: a batch exists to share one material binding, so
// batches can only be culled or drawn indirectly once nothing is bound per batch. See
// docs/bindless-attempt-notes.md for the ceiling the first attempt hit doing these in the wrong
// order.
//
// On by default. MAP10, MAP16, dbab01, dbab02 and dbab04 all render pixel-identical with it on --
// 0.0% over the world region against controls that also read 0.0%, loaded directly and through map
// changes -- and Sunder MAP16 goes from 165 draw calls at 0.69 ms to one at 0.50.
//
// It was off for one release of a day: the array was a STATIC variable, so following a texture to
// its next animation frame meant rebuilding all thirteen pipelines, shader compiles and all, twenty
// times a second. That was reported from play inside a minute -- mouse stutter, and a lowering floor
// whose sides could not decide what texture they were. The array is mutable now and lives on the
// bindings, so the same update is thirteen descriptor writes.
CVAR(Bool, fua_dg_bindless, true, CVAR_ARCHIVE)
// Sprites and decals on the shared binding as well. Off: see WorldSRB.
CVAR(Bool, fua_dg_bindless_dyn, true, 0)
// [rc4l] Build the level's walls from the map at load, rather than waiting for GL's walk of the BSP.
//
// ON by default. It was off while the BSP traversal was still what filled in the kinds the
// derivation did not do; the last of those was the 3D floor face -- the block a hanging slab cuts
// out of the wall behind it -- which is ffblocks_compute now, and closing it took dbab02 from 4.0%
// against the GL-driven picture to 0.0%.
//
// fua_surface_mapcover is the number that says how much of a level the map accounts for: 1332 of
// dbab04's 1336 wall parts, and 59,477 of Sunder MAP16's 59,483.
CVAR(Bool, fua_surface_mapbake_auto, true, 0)
// [rc4l] Collapse adjacent batches into one draw. Only ever possible with bindless on, and separable
// from it so "the array is wrong" and "the merge is wrong" can be told apart in one run.
CVAR(Bool, fua_dg_mergedraws, true, 0)
// [rc4l] Let the material array follow animated textures. Separable because following them means
// rebuilding thirteen bindings, and a binding the GPU is still reading is its own kind of wrong.
CVAR(Bool, fua_dg_bindless_anim, true, 0)
static int g_batchesDrawn = 0;

// Set once the level has been baked under standalone -- the bake itself needs GL frames.
static bool g_standaloneBaked = false;
static int  g_standaloneArmed = 0;

// [rc4l] The shader spells 64 and 24 out, because a GLSL string cannot read a C++ constant. This is
// the tie: change the grid and the build stops until the shader has been changed with it. A tile
// size that disagrees between the binning pass and the lookup is a grid where every cell holds the
// wrong lights, and it shows up as nothing more alarming than "the lighting is a bit off there".
static_assert(zx::hwrender::kClusterTilePixels == 64 && zx::hwrender::kClusterSlices == 24,
	"the cluster grid changed: update the 64 and 24 in FUA_LIGHT_GLSL to match.");

// [rc4l] The camera pitch, which lives outside any header the backend already pulls in.
extern int viewpitch;

// [rc4l] Which of the three ways a mark is drawn -- see projdecals.h. Declared at file scope, not
// inside zx::hwrender: a cvar lives in the global namespace, and an EXTERN_CVAR inside a namespace
// declares a DIFFERENT symbol that links against nothing.
EXTERN_CVAR(Int, fua_decalmode)

namespace zx { namespace hwrender {

// [rc4l] Backend chatter, off by default: here, when the scene targets are rebuilt and at what size.
EXTERN_CVAR(Bool, fua_dg_verbose)

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
// [rc4l] What a piece knows about itself, once, instead of once per vertex.
//
// Laid out in vec4s because std430 aligns a vec3 to 16 bytes anyway: writing it as four vec4s makes
// the padding explicit rather than something the GLSL and the C++ have to agree about by luck.
struct ScenePieceData
{
	float r, g, b, softLight;
	float fogDensity, alpha, fogMode, matSlot;
	float fogR, fogG, fogB, warpStyle;   // [rc4l] ANIMDEFS warp (1) or warp2 (2), else 0
	float nx, ny, nz, warpSpeed;
	// [rc4l] A flooded gap: u = floodU.xy . (x, y) + floodU.z on the plane at height floodU.w, v likewise, flood 1 to switch it on.
	float floodUx, floodUy, floodU0, floodZ;
	float floodVx, floodVy, floodV0, flood;
	// [rc4l] A sprite's or model's light from the CPU (GL's gl_SetDynSpriteLight), cpuLight 1 to use it instead of the shader's loop.
	float actorR, actorG, actorB, cpuLight;
	// [rc4l] The slot holding this material's brightmap in the bindless array (0 for none), the piece's PieceStackId plus one (0 for a sprite or model, never culled), 1 when both slots go through SlotRemap, and 1 when GL draws it unmasked so no texel is discarded.
	float brightSlot, stackId, animates, plain;
};

// [rc4l] The per-piece records the vertices index into, and the buffer that holds them.
//
// Sized per level with slack, like the vertex buffer, because a fixed capacity big enough for
// Sunder MAP16 would be most of what the shrink saves on a small map.
static TArray<ScenePieceData> g_scenePieceData;
static void FillPieceData(const zx::levelmesh::MeshPiece &p, int translation, ScenePieceData &pd, bool inMesh);

static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_pieceBuf;
// [rc4l] The stacked views' subsector bits (ComputeMarkStackVisible), sized per level like the piece records and bound beside them; ready once this frame's are uploaded.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_stackVisBuf;
static unsigned int g_stackVisCapacity = 0;
static bool g_stackVisReady = false;
static unsigned int g_pieceCapacity = 0;

static float g_mvp[16];
static int   g_drawRepeat = 1;
// [rc4l] Draw CALLS the opaque pass actually made, which is no longer the batch count.
static int   g_worldDraws = 0;

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
	// [rc4l] The box this batch occupies, so a batch nobody can see costs nothing to skip.
	//
	// Every batch was drawn every frame, whatever the camera was looking at -- correct, and on a
	// level with thousands of batches it is thousands of resource bindings a frame for geometry
	// behind the player. The GPU never minded (0.18 ms for the whole world); the CPU did.
	float        minX, minY, minZ;
	float        maxX, maxY, maxZ;
	// [rc4l] A reflective plane's sector (-1 for none) and side, and the reflection slot it drew from this frame.
	int          mirrorSector;
	bool         mirrorCeiling;
	int          mirrorSlot;
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
	// [rc4l] A closed shell rather than a billboard, and the difference decides the pipeline.
	//
	// Sprites and decals are flat, so they cannot occlude themselves and the sorted depth-write-off
	// pass is exactly right for them. A model is a solid: its near panels have to hide its far ones,
	// and without depth writes whichever triangle the buffer happens to hold last wins. On a car
	// that draws the interior floor and seats straight through the bodywork.
	bool         solid;
	int          mirrorLine;   // [rc4l] the viewer's own sprite, drawn only in this line mirror's reflection, or -1
	float        cx, cy, cz;   // centroid, for the merged sort
	float        lo, hi;       // [rc4l] its height span, which sides of a translucent plane it is on
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
// [rc4l] Patches, and how many vertices they moved -- the two numbers that say whether the buffer
// is being nudged or rewritten. A rebuild count alone cannot tell "nothing moved" from "everything
// moved and we patched it all".
static int g_geomPatches = 0, g_geomPatchVerts = 0;

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
Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::SHADER_TYPE type, const char *name, const char *source)
{
	Diligent::RefCntAutoPtr<Diligent::IShader> shader;
	auto *dev = GetDevice();
	if (dev == NULL) return shader;
	Diligent::ShaderCreateInfo ci;
	ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
	ci.Desc.ShaderType = type;
	ci.Desc.Name = name;
	ci.Source = source;
	dev->CreateShader(ci, &shader);
	return shader;
}

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

// [rc4l] The scene pipeline a static batch draws with.
static Diligent::IPipelineState *BatchPSO(int blend)
{
	switch (zx::hwrender::ComputeBatchPipeline(blend))
	{
	case zx::hwrender::kBatchPipelineMasked: return g_maskedPSO.RawPtr();
	case zx::hwrender::kBatchPipelineAdd: return g_addPSO.RawPtr();
	default: return g_transPSO.RawPtr();
	}
}

// [rc4l] A batch's binding, made again for the frame it last resolved to whenever a flush, a resize or a minimise dropped it.
static Diligent::IShaderResourceBinding *BatchSRB(SceneBatch &b)
{
	if (b.srb == NULL) b.srb = GetMaterialSRB(BatchPSO(b.blend), b.resolved);
	return b.srb;
}

// [rc4l] The material slot table: the level's textures, numbered.
//
// Slot 0 is always the white placeholder, so an overflow or an unresolvable material draws white
// rather than drawing whatever happens to be in slot 0.
//
// Keyed on (material, translation) for the same reason GetMaterialSRV is: a translated sprite is a
// different image of the same material, and giving them one slot paints the marine's armour on his
// twin.
// [rc4l] 512, and the number is not arbitrary.
//
// Sunder MAP16 uses 198 materials, so 256 is close enough to the cap to be a trap. The cost is
// slots x 13 pipelines = 6656 combined-image-sampler descriptors, and the ceiling this hit before
// -- 64 slots x 149 per-material bindings = 9536 worked, 128 x 149 = 19072 killed the device --
// puts the limit above 9.5k. 6656 sits under it with room, and does not move when a level has more
// materials, which is the whole difference between this shape and the one that failed.
// [rc4l] How many materials one bindless array holds -- and 512 was not enough for real content.
//
// Sunder and the eon maps fit in 512 comfortably, so every measurement in this work was taken with
// bindless ON and nothing suggested the cap mattered. A nine-PWAD Whodunit load wants 835, and past
// the cap MaterialSlotFor gives up and returns slot 0, which turns bindless OFF for the whole level:
// the world went from ONE draw call to 670, sprites from bindless to a descriptor binding each, and
// submit from 2.85 ms to 3.82. It is the loudest failure mode in the backend and it is a constant.
//
// 4096 is chosen to be past what content plausibly asks for rather than tuned to 835. The cost is a
// larger descriptor array, not a larger frame -- the array is sized once and indexed per pixel.
//
// The assumption worth naming: the device must support an array of this many sampled images in one
// descriptor set. Desktop Vulkan drivers report far more than this, but the Vulkan SPEC minimum is
// much lower, so a device that cannot will fail pipeline creation rather than degrade. If that is
// ever seen, the fix is to query maxPerStageDescriptorSampledImages and build the shader string at
// runtime instead of pinning it to FUA_MAT_SLOTS_STR.
void RecordPresentInterval();   // the frame-interval recorder, defined below

enum { kMaterialSlots = 4096 };
static_assert(kMaterialSlots == 4096, "FUA_MAT_SLOTS_STR must say the same number");

// [rc4l] `resolved` is what the slot actually POINTS AT, which is not always what it is keyed on.
//
// An animated texture keeps one FMaterial as its identity and swaps which image that resolves to
// every eight tics. The old path re-pointed the batch's binding; here the slot's contents change
// and its key does not, so the geometry never has to know that nukage flows.
struct MatSlot { const void *material; int translation; };
// [rc4l] The translation a brightmap's slot is filed under: no real one is this, so a material and its brightmap never share a slot.
static const int kBrightmapTranslation = -0x7fffffff;
static TArray<MatSlot> g_matSlotTable;
// [rc4l] Per slot, the slot showing its texture's current ANIMDEFS frame, on the GPU as SlotRemap; made once, since a static binding cannot follow a replaced buffer.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_slotRemapBuf;
static std::vector<unsigned> g_slotRemap;
static bool g_matSlotOverflow = false;
// The static array copied into the pipelines is only as fresh as the last fill. Anything that
// changes what a slot should point at -- a new material, an animated texture reaching its next
// frame -- raises this, and the next frame refills and rebuilds the world bindings.
static bool g_matSlotsDirty = true;
// Bounded retries for slots that came back white -- see FillMaterialArrayStatic.
static int g_matSlotRetries = 0;
// [rc4l] The level this table describes. Nothing is filled from it until the scene has been rebuilt
// for that level, because filling means calling GetMaterialSRV -- which CACHES what it makes, keyed
// on the FMaterial address. Doing that with the previous level's pointers, in the window before the
// new level's geometry arrives, teaches the cache things the new level then believes.
static int g_bindlessGen = -1;

// [rc4l] A side index over the slot table, because the table is SCANNED ONCE PER PIECE.
//
// MaterialSlotFor was a linear walk of the table, and every dynamic piece calls it to find out which
// texture it draws with. Sunder MAP16 has 3053 sprites a frame and 335 slots, so that is up to a
// million pointer comparisons a frame -- and it measured as 0.239 ms of the dynamic build's 0.493,
// half of it, and ten times what writing all the vertices costs.
//
// The table stays the source of truth: slot INDICES are what the shader reads, so they cannot move.
// This only answers "which index" without walking. Open addressing, no allocation, cleared with the
// table it indexes -- there are exactly two places that mutate it and both are next to this comment.
struct MatIndexEntry { const void *material; int translation; int slot; };
const int kMatIndexSize = 16384;   // power of two, comfortably over kMaterialSlots
static MatIndexEntry g_matIndex[kMatIndexSize];

static inline unsigned MatIndexHash(const void *m, int t)
{
	unsigned long long h = (unsigned long long)(size_t)m;
	h ^= (unsigned long long)(unsigned)t * 0x9E3779B97F4A7C15ull;
	h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
	return (unsigned)(h & (unsigned)(kMatIndexSize - 1));
}

// slot < 0 marks an empty bucket. NULL is a legitimate key -- it is the white slot -- so emptiness
// cannot be spelled with the material pointer.
static void MatIndexClear()
{
	for (int i = 0; i < kMatIndexSize; i++) g_matIndex[i].slot = -1;
}

static void MatIndexInsert(const void *material, int translation, int slot)
{
	unsigned i = MatIndexHash(material, translation);
	for (int probe = 0; probe < kMatIndexSize; probe++)
	{
		if (g_matIndex[i].slot < 0)
		{
			g_matIndex[i].material = material;
			g_matIndex[i].translation = translation;
			g_matIndex[i].slot = slot;
			return;
		}
		i = (i + 1) & (unsigned)(kMatIndexSize - 1);
	}
}

// -1 when absent.
static int MatIndexFind(const void *material, int translation)
{
	unsigned i = MatIndexHash(material, translation);
	for (int probe = 0; probe < kMatIndexSize; probe++)
	{
		const MatIndexEntry &e = g_matIndex[i];
		if (e.slot < 0) return -1;
		if (e.material == material && e.translation == translation) return e.slot;
		i = (i + 1) & (unsigned)(kMatIndexSize - 1);
	}
	return -1;
}

static void ResetMaterialSlots()
{
	g_matSlotTable.Clear();
	MatIndexClear();
	MatSlot white; white.material = NULL; white.translation = 0;
	g_matSlotTable.Push(white);
	MatIndexInsert(white.material, white.translation, 0);
	g_matSlotOverflow = false;
	g_matSlotRetries = 0;
	g_matSlotsDirty = true;
}

// [rc4l] What a slot holds: a material in its translation, or, under kBrightmapTranslation, that material's brightmap (black for none).
static Diligent::IDeviceObject *SlotSRV(const MatSlot &e)
{
	if (e.translation == kBrightmapTranslation) return GetBrightmapSRV(e.material);
	return GetMaterialSRV(e.material, e.translation);
}

static int PushMaterialSlot(const void *material, int translation)
{
	MatSlot e; e.material = material; e.translation = translation;
	g_matSlotTable.Push(e);
	const int slot = (int)g_matSlotTable.Size() - 1;
	MatIndexInsert(material, translation, slot);
	g_matSlotsDirty = true;
	return slot;
}

// [rc4l] The engine's animations as plain records and the one each texture index belongs to, rebuilt when ANIMDEFS or the texture list changes size.
static std::vector<zx::hwrender::AnimationDef> g_anims;
static std::vector<int> g_animOfTex;

static int AnimationOf(const FMaterial *mat)
{
	if (mat == NULL || mat->tex == NULL) return -1;
	const TArray<FAnimDef *> &defs = TexMan.GetAnimations();
	if (g_anims.size() != defs.Size() || g_animOfTex.size() != (size_t)TexMan.NumTextures())
	{
		g_anims.clear();
		for (unsigned i = 0; i < defs.Size(); i++)
		{
			zx::hwrender::AnimationDef a;
			a.basePic = defs[i]->BasePic.GetIndex();
			a.numFrames = defs[i]->NumFrames;
			a.discrete = defs[i]->AnimType == FAnimDef::ANIM_DiscreteFrames;
			if (a.discrete)
				for (int f = 0; f < defs[i]->NumFrames; f++) a.pics.push_back(defs[i]->Frames[f].FramePic.GetIndex());
			g_anims.push_back(a);
		}
		g_animOfTex = zx::hwrender::ComputeAnimationOfTexture(TexMan.NumTextures(), g_anims);
	}
	const int id = mat->tex->id.GetIndex();
	return (id >= 0 && id < (int)g_animOfTex.size()) ? g_animOfTex[id] : -1;
}

int MaterialSlotFor(const void *material, int translation)
{
	if (g_matSlotTable.Size() == 0) ResetMaterialSlots();
	{
		const int found = MatIndexFind(material, translation);
		if (found >= 0) return found;
	}
	if (g_matSlotTable.Size() >= (unsigned)kMaterialSlots)
	{
		// Loud, once: a level past the slot count draws its extra materials white, and white walls
		// with no message is the kind of thing that gets diagnosed as a texture-loading fault.
		if (!g_matSlotOverflow)
			Printf("Diligent bindless: more than %d materials on this level -- falling back to a binding per material"
				"\n", (int)kMaterialSlots);
		g_matSlotOverflow = true;
		return 0;
	}
	const int slot = PushMaterialSlot(material, translation);
	// [rc4l] Every frame of an animated texture gets a slot right behind it, so SlotRemap can point at any frame without a new binding.
	const int anim = AnimationOf((const FMaterial *)material);
	if (anim >= 0)
		for (const int frame : zx::hwrender::ComputeAnimationFrames(g_anims[anim]))
		{
			FMaterial *fm = FMaterial::ValidateTexture(TexMan.ByIndex(frame), false);
			if (fm != NULL && MatIndexFind(fm, translation) < 0 && g_matSlotTable.Size() < (unsigned)kMaterialSlots)
				PushMaterialSlot(fm, translation);
		}
	return slot;
}

int MaterialSlotCount() { return (int)g_matSlotTable.Size(); }
// Force the array to be built again, for telling a stale fill from a wrong index.
void DirtyMaterialSlots() { g_matSlotsDirty = true; g_matSlotRetries = 0; }



// [rc4l] ONE binding per pipeline, which is the whole point.
//
// A STATIC shader variable is copied into every SRB made from the pipeline, and this backend makes
// one SRB per (pipeline, material) -- so a 256-slot array bound the old way would cost 256 times the
// material count in descriptors and take the device down. Bindless replaces those bindings rather
// than joining them: with the array in place there is nothing left that varies per material, so
// thirteen bindings serve the whole world. See docs/bindless-attempt-notes.md.
static Diligent::IPipelineState *g_worldPSOs[13] = { 0 };
static Diligent::IShaderResourceBinding *g_worldSRBs[13] = { 0 };
static bool g_bindlessReady = false;
static int g_matSlotsWhite = 0;
// Per pipeline: '-' untouched, 'x' the array is not there, 'f' filled, 'b' filled and bound.
static char g_fillState[14] = "-------------";
static FString g_fillNames[8];
static const void *g_fillViews[8] = { 0 };
static int g_fillCount = 0;
// [rc4l] Uploads of SlotRemap, one per frame an animation moved.
static int g_remapUploads = 0;
// [rc4l] What the array was last filled FROM, so a rebuild that changes nothing rebuilds nothing.
//
// A scene rebuild resets the slot table and re-emits every vertex, and on a level with moving
// sectors that happens constantly -- dbab04 rebuilds the scene about two hundred times in a minute.
// The table it produces is almost always IDENTICAL to the one before it, so taking the reset as a
// reason to rebuild thirteen shader resource bindings meant rebuilding them every frame, and
// releasing a binding the GPU is still reading out of the previous frame's command buffer.
static TArray<MatSlot> g_filledFrom;

static bool MaterialSlotsMatchFill()
{
	if (g_filledFrom.Size() != g_matSlotTable.Size()) return false;
	for (unsigned i = 0; i < g_matSlotTable.Size(); i++)
		if (g_filledFrom[i].material != g_matSlotTable[i].material ||
		    g_filledFrom[i].translation != g_matSlotTable[i].translation) return false;
	return true;
}

static int g_dynSlotRefused = 0, g_dynSlotNoMaterial = 0, g_dynSlotSeen = 0, g_dynSlotMax = 0;
// [rc4l] The viewer's own sprites in this frame's dynamic stream, one per line mirror GL walked at the first level.
static int g_dynViewerRuns = 0;
static int g_dynBindless = 0, g_dynPerMaterial = 0;
// What the scene pixel shader said it declares, recorded at creation so the question can be asked
// later -- pipeline setup happens before anything is listening on the console.
static FString g_scenePSResources;

static void ReleaseMaterialSRBs();
static void ReleaseWorldSRBs()
{
	for (int i = 0; i < 13; i++)
		if (g_worldSRBs[i]) { g_worldSRBs[i]->Release(); g_worldSRBs[i] = NULL; }
}

// [rc4l] Fill the material array on a BINDING.
//
// Every element, always: an unassigned element is not "mostly bound", and the white placeholder
// covers the slots past the live ones as well as anything whose own upload failed.
static bool FillMaterialArraySRB(Diligent::IShaderResourceBinding *srb)
{
	if (!srb) return false;
	auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uMaterials");
	Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
	if (v == NULL || white == NULL)
	{
		static bool said = false;
		if (!said)
		{
			said = true;
			Printf("Diligent bindless: %s; the shader declared:%s" "\n",
				v ? "no white placeholder texture" : "uMaterials is not on the binding",
				g_scenePSResources.GetChars());
		}
		return false;
	}
	g_matSlotsWhite = 0;
	for (Diligent::Uint32 i = 0; i < (Diligent::Uint32)kMaterialSlots; i++)
	{
		Diligent::IDeviceObject *obj = NULL;
		if (i < g_matSlotTable.Size())
			obj = SlotSRV(g_matSlotTable[i]);
		const bool live = (i < g_matSlotTable.Size()) && g_matSlotTable[i].material != NULL;
		if (obj == NULL) obj = white;
		// [rc4l] A slot that came back WHITE for a real material is not filled, it is wrong.
		//
		// GetMaterialSRV falls back to white when it cannot produce a texture yet -- a sprite whose
		// image has not been uploaded on the frame the array happens to be built. Filled once and
		// cached, that fallback is permanent, so a white answer for a live material asks for the
		// array to be built again, with a bound on the retries.
		if (live && obj == white)
		{
			g_matSlotsWhite++;
			if (g_matSlotRetries < 120) { g_matSlotsDirty = true; g_matSlotRetries++; }
		}
		if (i < 8 && live)
		{
			FMaterial *fm = (FMaterial *)g_matSlotTable[i].material;
			g_fillNames[i] = (fm && fm->tex) ? fm->tex->Name.GetChars() : "?";
			g_fillViews[i] = (const void *)obj;
		}
		v->SetArray(&obj, i, 1);
	}
	return true;
}



// Declared here because refreshing the array can require the pipelines to be made again -- see
// the comment inside RefreshBindless for why that is not optional.
static void ReleaseScenePipelines();
static void ReleasePlaneMirrors();
static void ReleaseLineMirrorPipeline();
static void ReleaseStackPortals();
static bool EnsureScenePipeline(FString &err);

static void RefreshBindless()
{
	if (zx::levelmesh::LevelGeneration() != g_bindlessGen)
	{
		ReleaseWorldSRBs();
		g_bindlessReady = false;
		return;
	}
	// [rc4l] Animated textures move through SlotRemap, since Diligent ignores a second write to a bound mutable descriptor and rewriting one the GPU still reads is undefined.
	if (fua_dg_animate && g_bindlessReady && g_slotRemapBuf)
	{
		const bool moved = zx::hwrender::ComputeSlotRemap(g_matSlotTable.Size(), [](unsigned i) -> int {
			const MatSlot &e = g_matSlotTable[i];
			const FMaterial *base = (const FMaterial *)e.material;
			if (base == NULL || base->tex == NULL) return -1;
			FMaterial *now = FMaterial::ValidateTexture(base->tex->id, false, true);
			return (now == NULL || now == base) ? -1 : MatIndexFind(now, e.translation);
		}, g_slotRemap);
		auto *ctx = GetContext();
		if (moved && ctx)
		{
			ctx->UpdateBuffer(g_slotRemapBuf, 0, (Diligent::Uint64)g_slotRemap.size() * sizeof(unsigned), &g_slotRemap[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			g_remapUploads++;
		}
	}
	if (!g_matSlotsDirty) return;
	g_matSlotsDirty = false;
	// A table that has not changed needs no new bindings -- see g_filledFrom.
	if (g_bindlessReady && MaterialSlotsMatchFill()) return;
	g_bindlessReady = false;
	g_filledFrom.Clear();
	ReleaseWorldSRBs();
	for (int i = 0; i < 13; i++)
	{
		if (!g_worldPSOs[i]) return;
		g_worldPSOs[i]->CreateShaderResourceBinding(&g_worldSRBs[i], true);
		if (!g_worldSRBs[i]) { ReleaseWorldSRBs(); return; }
		g_fillState[i] = 'b';
		// uTex and uBrightmap are still declared and still mutable. The shader does not read them
		// while the array is live, but an unassigned sampler is undefined rather than unused.
		Diligent::IDeviceObject *white = GetMaterialSRV(NULL, 0);
		if (auto *v = g_worldSRBs[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(white);
		if (auto *v = g_worldSRBs[i]->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uBrightmap"))
		{
			Diligent::IDeviceObject *bm = GetBrightmapSRV(NULL);
			v->Set(bm ? bm : white);
		}
		if (!FillMaterialArraySRB(g_worldSRBs[i]))
		{
			ReleaseWorldSRBs();
			static bool said = false;
			if (!said) { said = true; Printf("Diligent bindless: staying on per-material bindings" "\n"); }
			return;
		}
	}
	g_bindlessReady = true;
	g_fillCount++;
	g_filledFrom = g_matSlotTable;
	static bool announced = false;
	if (!announced)
	{
		announced = true;
		Printf("Diligent bindless: %d materials in %d slots, 13 bindings for the whole world" "\n",
			MaterialSlotCount(), (int)kMaterialSlots);
	}
}

// [rc4l] A level with more materials than slots gives the whole thing up, rather than drawing the
// extras white.
//
// The overflow path has to be the SAFE one, because it is the one nobody tests: a map big enough to
// reach it is by definition a map that was not to hand. Falling back to per-material bindings costs
// a little submit time on one level; drawing white walls costs a bug report that looks like a
// texture-loading fault.
// See the CCMD: drop everything keyed on an FMaterial address and let it all be made again.
void FlushTextureCache()
{
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	for (unsigned b = 0; b < g_batches.Size(); b++) g_batches[b].srb = NULL;
	ReleaseWorldSRBs();
	ReleaseMaterials();
	g_bindlessReady = false;
	DirtyMaterialSlots();
}

bool BindlessActive() { return fua_dg_bindless && g_bindlessReady && !g_matSlotOverflow; }

// The binding to commit for a draw on this pipeline, or NULL if bindless is not carrying it.
static Diligent::IShaderResourceBinding *WorldSRB(Diligent::IPipelineState *pso)
{
	if (!BindlessActive()) return NULL;
	for (int i = 0; i < 13; i++)
		if (g_worldPSOs[i] == pso)
		{
			// [rc4l] Sprites and decals are on the array too, and the switch stays for bisecting.
			//
			// They used to draw as flat white rectangles, which looked like a second bug and was the same
			// one: a sprite whose material joined the table after the array had been filled could not be
			// added to it, because a static variable cannot be rewritten once its pipeline has handed out
			// a binding. Rebuilding the pipelines fixed both at once.
			if (i >= 4 && i < 12 && !fua_dg_bindless_dyn) return NULL;
			return g_worldSRBs[i];
		}
	return NULL;
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

// [rc4l] How many SRBs are alive, for fua_dg_srbcost -- see the CCMD for why the number matters.
void MaterialSRBCounts(int &cached, int &batch)
{
	cached = (int)g_matSRBs.Size();
	batch = (int)g_batchSRBs.Size();
}

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
static bool g_pieceBindFailed = false;

// [rc4l] The per-frame sprite geometry: uploaded fresh every frame, never cached.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_dynVB;
static unsigned int g_dynVBCapacity = 0;

// [rc4l] The SORTED ORDER, as an index buffer, so back-to-front costs one draw instead of hundreds.
//
// The translucent pass sorts by distance and cannot batch by material, so it used to issue a draw
// per surface: 1380 sprites in an arena on Sunder MAP10 came to 931 submissions even after adjacent
// ones were collapsed, because the sort scatters neighbours. At roughly two microseconds of driver
// time each that is most of the frame's CPU cost, on a map where a ninth of the pixels only buys a
// third of the time.
//
// Writing the sorted order into indices instead moves the ordering out of the draw list and into
// the buffer, where it costs nothing: every surface that wants the same pipeline becomes one
// DrawIndexed over a contiguous span of indices, and the order within that span is exactly the order
// the sort produced. Nothing is reordered and nothing is merged that was not adjacent in the sorted
// list -- only the submissions disappear.
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_blendIB;
static unsigned int g_blendIBCapacity = 0;
static TArray<Diligent::Uint32> g_blendIndices;

static bool EnsureBlendIndexBuffer(Diligent::IRenderDevice *dev, unsigned int needed)
{
	if (dev == NULL || needed == 0) return false;
	if (g_blendIB && g_blendIBCapacity >= needed) return true;
	// Grown with slack, because a frame that needs one more index than the last must not reallocate.
	unsigned int cap = g_blendIBCapacity ? g_blendIBCapacity : 4096;
	while (cap < needed) cap *= 2;
	g_blendIB.Release();
	Diligent::BufferDesc bd;
	bd.Name = "fua blend order";
	bd.Size = (Diligent::Uint64)cap * sizeof(Diligent::Uint32);
	bd.BindFlags = Diligent::BIND_INDEX_BUFFER;
	bd.Usage = Diligent::USAGE_DEFAULT;
	dev->CreateBuffer(bd, NULL, &g_blendIB);
	if (!g_blendIB) { g_blendIBCapacity = 0; return false; }
	g_blendIBCapacity = cap;
	return true;
}
static int g_dynDraws = 0, g_dynTris = 0;
// [rc4l] What was actually SUBMITTED, as opposed to queued. g_dynDraws counts entries in the sorted
// list -- what the pass has to order -- and not what it hands the driver, and the two stopped being
// the same number when adjacent draws started being collapsed.
static int g_blendSubmits = 0;
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
	// [rc4l] The piece this vertex belongs to. Everything else moved into ScenePieceData.
	//
	// Nineteen floats a vertex was the price of having no per-draw state: colour, light, fog, the
	// surface normal and the material slot are all constant across a piece, and they were duplicated
	// onto every one of its vertices because a uniform would have meant a draw per piece. An INDEX
	// costs one float and buys the same thing -- the same trick bindless plays with textures, and for
	// the same reason. On Sunder MAP16 that is 796,566 vertices at 76 bytes coming down to 24.
	float pieceIndex;
};

// [rc4l] A GL vertex (x, y, height) as the mesh stores it (x, height, y), tagged with its piece: the one conversion the full build, a patch and every per-frame stream share.
static SceneVertex ToSceneVertex(const FFlatVertex &sv, unsigned int pieceIndex)
{
	SceneVertex dv;
	dv.x = sv.x; dv.y = sv.z; dv.z = sv.y;
	dv.u = sv.u; dv.v = sv.v;
	dv.pieceIndex = (float)pieceIndex;
	return dv;
}

// [rc4l] Uploads count vertices of stride bytes into a grow-only vertex buffer, re-created at ComputeGrownCapacity when short; false if none can be had.
//
// USAGE_DEFAULT + UpdateBuffer rather than a mapped dynamic buffer: this data changes once per
// frame, so it does not belong in the dynamic heap at all.
// [rc4l] A w x h colour target the shaders can read and its depth buffer, in the swapchain's formats, for a view rendered off screen; false if either cannot be made.
static bool CreateViewTextures(int w, int h, const char *name,
	Diligent::RefCntAutoPtr<Diligent::ITexture> &color, Diligent::RefCntAutoPtr<Diligent::ITexture> &depth)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || w <= 0 || h <= 0) return false;
	const auto &sd = swap->GetDesc();
	FString colorName, depthName;
	colorName.Format("%s colour", name);
	depthName.Format("%s depth", name);
	Diligent::TextureDesc td;
	td.Name = colorName.GetChars();
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = (Diligent::Uint32)w; td.Height = (Diligent::Uint32)h;
	td.Format = sd.ColorBufferFormat;
	td.MipLevels = 1;
	td.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
	color.Release();
	dev->CreateTexture(td, nullptr, &color);
	Diligent::TextureDesc dd = td;
	dd.Name = depthName.GetChars();
	dd.Format = sd.DepthBufferFormat;
	dd.BindFlags = Diligent::BIND_DEPTH_STENCIL;
	depth.Release();
	dev->CreateTexture(dd, nullptr, &depth);
	return color.RawPtr() != nullptr && depth.RawPtr() != nullptr;
}

static bool UploadGrowingVB(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const void *data, unsigned int count, unsigned int stride, unsigned int pad, const char *name)
{
	if (!vb || capacity < count)
	{
		capacity = zx::hwrender::ComputeGrownCapacity(count, pad);
		Diligent::BufferDesc bd;
		bd.Name = name;
		bd.Size = (Diligent::Uint64)capacity * stride;
		bd.Usage = Diligent::USAGE_DEFAULT;
		bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
		vb.Release();
		GetDevice()->CreateBuffer(bd, nullptr, &vb);
		if (!vb) { capacity = 0; return false; }
	}
	ctx->UpdateBuffer(vb, 0, (Diligent::Uint64)count * stride, data, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	return true;
}


static TArray<SceneVertex> g_sceneVB;

// [rc4l] Same vertex layout as FFlatVertex (3 float position, 2 float uv), so the level mesh's data
// is uploaded byte-for-byte with no conversion pass.
// [rc4l] kMaxStackViews spelled for the scene shader, which marks one bit per view for every subsector.
#define FUA_STACK_VIEWS_STR "2"
static_assert(zx::levelmesh::kMaxStackViews == 2, "FUA_STACK_VIEWS_STR spells kMaxStackViews");
static const char *kSceneVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in float aPiece;\n"
	FUA_CONSTANTS_GLSL
	// [rc4l] Everything constant across a piece, read once here instead of ridden in on every vertex.
	FUA_PIECEDATA_GLSL
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec3 vColor;\n"
	"layout(location = 2) out vec3 vLightParm;\n"
	"layout(location = 3) out vec4 vFog;\n"
	"layout(location = 4) out vec4 vPixelPos;\n"
	"layout(location = 5) flat out int vLightIndex;\n"
	"layout(location = 6) out vec3 vNormal;\n"
	"layout(location = 7) flat out vec4 vPlane;\n"
	"layout(location = 8) flat out vec2 vWarp;\n"
	"layout(location = 9) flat out vec4 vFloodU;\n"
	"layout(location = 10) flat out vec4 vFloodV;\n"
	"layout(location = 11) flat out vec4 vActorLight;\n"
	"layout(location = 12) flat out int vBrightSlot;\n"
	"layout(location = 13) flat out int vLinkId;\n"
	"layout(location = 14) flat out float vPlain;\n"
	"layout(std430, binding = 10) readonly buffer StackVis { uint stackVis[]; };\n"
	"layout(std430, binding = 11) readonly buffer SlotRemap { uint slotRemap[]; };\n"
	"void main() {\n"
	"    PieceData pd = pieces[int(aPiece + 0.5)];\n"
	"    vWarp = vec2(pd.fogColor.w, pd.normal.w);\n"
	"    vFloodU = pd.floodU;\n"
	"    vFloodV = pd.floodV;\n"
	"    vActorLight = pd.actorLight;\n"
	"    vBrightSlot = int(pd.bright.x + 0.5);\n"
	// [rc4l] bright.z marks a baked piece on an animated texture, whose slots SlotRemap turns into those of the frame showing.
	"    if (pd.bright.z > 0.5) vBrightSlot = int(slotRemap[vBrightSlot]);\n"
	// [rc4l] The piece's subsector or sidedef as GL's light links name it (bright.y is PieceStackId plus one), or -1 for a sprite or model.
	"    vLinkId = int(pd.bright.y + 0.5) - 1;\n"
	"    vPlain = pd.bright.w;\n"
	"    vec4 clip = uMVP * vec4(aPos, 1.0);\n"
	// [rc4l] A stacked view draws only what GL's walk through it drew, subsector for a flat and sidedef for a wall (ComputeMarkStackVisible); any other piece's triangle goes off screen whole.
	"    int stackSlot = int(uFrame.y + 0.5) - 1;\n"
	"    int stackId = int(pd.bright.y + 0.5) - 1;\n"
	"    if (stackSlot >= 0) {\n"
	"        int bit = stackId * " FUA_STACK_VIEWS_STR " + stackSlot;\n"
	"        bool unseen = stackId >= 0 && (stackVis[bit >> 5] & (1u << uint(bit & 31))) == 0u;\n"
	// [rc4l] GL draws a surface only from its front, a floor from above and a ceiling from below, and a stacked view's eye sits under every rooftop it looks up at.
	"        if (unseen || dot(pd.normal.xyz, uCameraPos.xyz - aPos) < -0.01) clip = vec4(2.0, 2.0, 2.0, 1.0);\n"
	"    }\n"
	"    gl_Position = clip;\n"
	"    vUV = aUV;\n"
	"    vColor = pd.colorLight.rgb;\n"
	// softLight, fogDensity, alpha -- the three the fragment shader reads as vLightParm.
	"    vLightParm = vec3(pd.colorLight.w, pd.fogAlphaMat.x, pd.fogAlphaMat.y);\n"
	"    vFog = vec4(pd.fogColor.rgb, pd.fogAlphaMat.z);\n"
	// [rc4l] main.vp keeps pixelpos as (world xyz, view depth). The view depth is what the plane
	// fog mode measures along, and clip.w IS that depth for a standard perspective matrix -- the
	// projection's -1 in the w row copies eye-space -z straight through.
	"    vPixelPos = vec4(aPos, clip.w);\n"
	"    vLightIndex = int(pd.fogAlphaMat.w);\n"
	"    if (pd.bright.z > 0.5) vLightIndex = int(slotRemap[vLightIndex]);\n"
	"    vNormal = pd.normal.xyz;\n"
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
	"    vPlane = vec4(pd.normal.xyz, dot(pd.normal.xyz, aPos));\n"
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
// Must match kMaterialSlots. A GLSL string cannot read a C++ constant, so the two are tied by the
// static_assert below and by nothing else -- a mismatch is a fatal at pipeline setup, one message
// per unassigned element.
#define FUA_MAT_SLOTS_STR "4096"

#define FUA_LIGHT_GLSL \
	"#extension GL_EXT_nonuniform_qualifier : require\n" \
	"layout(location = 0) in vec2 vUV;\n" \
	"layout(location = 1) in vec3 vColor;\n" \
	"layout(location = 2) in vec3 vLightParm;\n" \
	"layout(location = 3) in vec4 vFog;\n" \
	"layout(location = 4) in vec4 vPixelPos;\n" \
	"layout(location = 5) flat in int vLightIndex;\n" \
	"layout(location = 6) in vec3 vNormal;\n" \
	"layout(location = 7) flat in vec4 vPlane;\n" \
	"layout(location = 8) flat in vec2 vWarp;\n" \
	"layout(location = 9) flat in vec4 vFloodU;\n" \
	"layout(location = 10) flat in vec4 vFloodV;\n" \
	"layout(location = 11) flat in vec4 vActorLight;\n" \
	"layout(location = 12) flat in int vBrightSlot;\n" \
	"layout(location = 13) flat in int vLinkId;\n" \
	"layout(location = 14) flat in float vPlain;\n" \
	FUA_CONSTANTS_GLSL \
	"layout(binding = 1) uniform sampler2D uTex;\n" \
	/* [rc4l] The brightmap layer, present on EVERY material -- black where there is none.
	   The brightmap is added to the lit colour, so black is the identity, and that removes the
	   alternative: a per-batch flag, a branch here, and a second pipeline permutation. */ \
	"layout(binding = 5) uniform sampler2D uBrightmap;\n" \
	/* [rc4l] Every material in the level, reachable from one descriptor set.

	   uSkyColor.w carries the live slot count, so zero means "use the bound texture" and the whole
	   thing switches from one constant, with no second pipeline and no shader permutation. The index
	   is vLightIndex -- the vertex field that used to hold a dynamic light index and has been dead
	   since this shader started testing every light -- so bindless costs no vertex bytes.

	   nonuniformEXT is not decoration: neighbouring fragments in one quad can sit on different
	   surfaces with different slots, and without it the index is assumed uniform across the wave
	   and the whole quad samples whichever one won. */ \
	"layout(binding = 8) uniform sampler2D uMaterials[" FUA_MAT_SLOTS_STR "];\n" \
	/* [rc4l] GL's func_warp1.fp and func_warp2.fp, line for line -- see warp_compute for the tested copy. */ \
	"vec2 fuaWarp(vec2 uv) {\n" \
	"    if (vWarp.x < 0.5) return uv;\n" \
	"    const float twoPi = 6.28318530717958647692;\n" \
	"    float t = uFrame.x * vWarp.y;\n" \
	"    vec2 o;\n" \
	"    if (vWarp.x < 1.5) {\n" \
	"        o.y = sin(twoPi * (uv.x + t * 0.125)) * 0.1;\n" \
	"        o.x = sin(twoPi * (uv.y + t * 0.125)) * 0.1;\n" \
	"    } else {\n" \
	"        float siny = sin(twoPi * (uv.y * 2.2 + t * 0.75)) * 0.03;\n" \
	"        o.y = siny + sin(twoPi * (uv.x * 0.75 + t * 0.75)) * 0.03;\n" \
	"        o.x = siny + sin(twoPi * (uv.x * 1.1 + t * 0.45)) * 0.02;\n" \
	"    }\n" \
	"    return uv + o;\n" \
	"}\n" \
	/* [rc4l] GL's flooded gap: where the eye's ray through this fragment meets the plane at vFloodU.w, w 1 when it does; flood_compute is the tested twin. */ \
	"vec4 fuaFloodPoint() {\n" \
	"    if (vFloodV.w < 0.5) return vec4(0.0);\n" \
	"    float rise = vPixelPos.y - uCameraPos.y;\n" \
	"    if (rise == 0.0) return vec4(0.0);\n" \
	"    float t = (vFloodU.w - uCameraPos.y) / rise;\n" \
	"    if (!(t > 0.0 && t <= 1.0)) return vec4(0.0);\n" \
	"    return vec4(uCameraPos.xyz + t * (vPixelPos.xyz - uCameraPos.xyz), 1.0);\n" \
	"}\n" \
	/* [rc4l] The plane's own texture coordinate there (mesh x and z are map x and y), or the wall's own when the eye is on the plane's far side. */ \
	"vec2 fuaFloodUV(vec2 uvIn) {\n" \
	"    vec4 q = fuaFloodPoint();\n" \
	"    if (q.w < 0.5) return uvIn;\n" \
	"    return vec2(dot(vFloodU.xy, q.xz) + vFloodU.z, dot(vFloodV.xy, q.xz) + vFloodV.z);\n" \
	"}\n" \
	"vec4 fuaTexel(vec2 uvIn) {\n" \
	"    vec2 uv = fuaWarp(fuaFloodUV(uvIn));\n" \
	"    if (uSkyColor.w > 0.5 && vLightIndex > 0)\n" \
	"        return texture(uMaterials[nonuniformEXT(vLightIndex)], uv);\n" \
	"    return texture(uTex, uv);\n" \
	"}\n" \
	"layout(std430, binding = 2) readonly buffer LightBuffer { vec4 lights[]; };\n" \
	/* [rc4l] The cluster grid: (offset, count) per cell, and the runs those point into. */ \
	"layout(std430, binding = 6) readonly buffer ClusterTable { uvec2 clusterCells[]; };\n" \
	"layout(std430, binding = 7) readonly buffer ClusterLightIndices { uint clusterLights[]; };\n" \
	/* [rc4l] ComputeLightTouches, line for line: a light lights only the ids GL linked it to, and a count of all ones lights everything. */ \
	"layout(std430, binding = 12) readonly buffer LightTouchRanges { uvec2 lightTouch[]; };\n" \
	"layout(std430, binding = 13) readonly buffer LightTouchIds { uint touchIds[]; };\n" \
	"bool fuaLightTouches(int li, uint id) {\n" \
	"    uvec2 r = lightTouch[li];\n" \
	"    if (r.y == 0xFFFFFFFFu) return true;\n" \
	"    uint lo = r.x, hi = r.x + r.y;\n" \
	"    while (lo < hi) {\n" \
	"        uint mid = (lo + hi) >> 1;\n" \
	"        uint v = touchIds[mid];\n" \
	"        if (v == id) return true;\n" \
	"        if (v < id) lo = mid + 1u; else hi = mid;\n" \
	"    }\n" \
	"    return false;\n" \
	"}\n" \
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
	/* [rc4l] The cell this fragment is in, and the short list of lights that can reach it.

	   uLightParams.zw carry the cluster depth range, and a zero in z means the grid is off -- in
	   which case this loops over every light exactly as it always has. The two paths must draw the
	   SAME picture, so fua_dg_clusters flips between them in a frozen frame and a diff says whether
	   they do.

	   Depth is 1.0 / gl_FragCoord.w: the clip w the hardware interpolated, which is the quantity
	   ComputeLightClustersFromMVP slices the light by on the CPU. Two sides agreeing because they
	   read the same number is worth more than two sides computing numbers that ought to match.

	   The 64 and 24 are kClusterTilePixels and kClusterSlices; a static_assert keeps them honest. */ \
	"    int first = 0;\n" \
	"    int count = n;\n" \
	"    bool clustered = uLightParams.z > 0.0 && uLightParams.w > uLightParams.z;\n" \
	"    if (clustered) {\n" \
	"        int tilesX = int((uScreen.x + 63.0) / 64.0);\n" \
	"        int tilesY = int((uScreen.y + 63.0) / 64.0);\n" \
	"        int tx = clamp(int(gl_FragCoord.x) / 64, 0, tilesX - 1);\n" \
	"        int ty = clamp(int(gl_FragCoord.y) / 64, 0, tilesY - 1);\n" \
	"        float depth = 1.0 / gl_FragCoord.w;\n" \
	"        int slice = 0;\n" \
	"        if (depth > uLightParams.z)\n" \
	"            slice = clamp(int(floor(log(depth / uLightParams.z) /\n" \
	"                log(uLightParams.w / uLightParams.z) * 24.0)), 0, 23);\n" \
	"        uvec2 cell = clusterCells[(slice * tilesY + ty) * tilesX + tx];\n" \
	"        first = int(cell.x);\n" \
	"        count = int(cell.y);\n" \
	/* [rc4l] CLAMP the loop bound. It comes out of a buffer, and a buffer can be stale.\
	   Every light in the level is at most n, so anything larger is nonsense -- and nonsense is what\
	   the cluster grid holds around a level change, before the CPU rebuilds it for the new map. A\
	   garbage count runs this loop for millions of iterations, and a GPU loop that long IS a hang:\
	   Windows times it out, resets the device, and the driver terminates the process -- no\
	   exception, no dump, nothing in the log. See docs/tdr-crash-after-map-loads.md. One\
	   instruction makes a stale grid a wrong pixel for one frame instead of a dead process. */ \
	"        count = clamp(count, 0, n);\n" \
	"        first = max(first, 0);\n" \
	"    }\n" \
	"    for (int i = 0; i < count; i++) {\n" \
	/* ...and the index it reads: lights[] has n entries and a stale grid can name anything. An\
	   out-of-range read is undefined, not merely wrong. */ \
	"        int li = clustered ? int(clusterLights[first + i]) : i;\n" \
	"        li = clamp(li, 0, max(n - 1, 0));\n" \
	/* [rc4l] GL's flood stops at solid walls, so a light whose links do not name this surface does not light it. */ \
	"        if (vLinkId >= 0 && !fuaLightTouches(li, uint(vLinkId))) continue;\n" \
	"        vec4 lp = lights[li*2];\n" \
	"        vec4 lc = lights[li*2+1];\n" \
	"        vec3 d = lp.xyz - vPixelPos.xyz;\n" \
	/* The side test gl_GetLight does per surface: a light behind the plane does not light it.
	   Without this the backs of walls and the room next door get lit, which reads as a scene far
	   more saturated than GL's. */ \
	/* [rc4l] ...and the same test read off the plane instead of the fragment.

	   dot(n, lightPos) - planeD is dot(n, lightPos - fragPos) for any fragment ON the plane, so this
	   is the same quantity, computed from numbers that do not vary across the face. A light lying
	   exactly in the plane gives exactly zero and is KEPT, which is what gl_flats.cpp does: it drops
	   a light only when the plane is strictly on the wrong side of it. */ \
	/* [rc4l] ...and a light sitting exactly ON the plane is KEPT, which needs slack to survive.

	   gl_flats.cpp drops a light only when the plane is STRICTLY above it, so a light exactly in the
	   plane stays -- and Doom puts lights exactly in planes constantly. A projectile that dies on a
	   floor comes to rest ON it, so its z equals the plane there to the last bit of fixed point:
	   measured on dbab04 as a plasma ball at -59.053 under a floor plane of -59.053. GL keeps that
	   light. This test computes the same quantity in floats, from a normalised normal and a plane
	   constant built out of vertex coordinates in the thousands, so the exact zero arrives as a few
	   thousandths either side of it -- and on the wrong side an entire surface is culled at once.

	   That is what the reported hard cut-off was. Every plasma bolt landing on that floor lit the
	   walkway above and left the floor it was resting on completely black, along a dead straight line
	   at the piece boundary, in Vulkan only. GL 30.6 against Vulkan 0.0 over the same rectangle of
	   the same frozen frame.

	   The slack is a tenth of a map unit: thousands of times the float error being covered, and far
	   below anything a player could see -- a light a tenth of a unit behind a surface lights it now.
	   It is not an attempt to place the boundary exactly, which floats cannot do; it is to keep the
	   case Doom actually produces on the same side of it as GL. */ \
	"        if (dot(vPlane.xyz, lp.xyz) - vPlane.w < -0.1) continue;\n" \
	"        float a = max(lp.w - length(d), 0.0) / lp.w;\n" \
	"        if (a <= 0.0) continue;\n" \
	"        if (lc.a > 0.5) dyn -= lc.rgb * a;\n" \
	"        else            dyn += lc.rgb * a;\n" \
	"    }\n" \
	"    return clamp(base + dyn, 0.0, 1.4);\n" \
	"}\n" \
	FUA_SECTOR_LIGHT_GLSL \
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
	/* [rc4l] 16: the MATERIAL SLOT the fragment is using, as a grey ramp; 17: whether it is
	   taking the array at all. A surface that comes out white with bindless on looks the same
	   whether the slot is zero, the array is empty, or the switch is off, and these separate the
	   three without reading it off a guess. */ \
	"    if (dbg == 16.0) return vec3(float(vLightIndex) / 255.0);\n" \
	"    if (dbg == 17.0) return (uSkyColor.w > 0.5 && vLightIndex > 0) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n" \
	"    if (dbg == 0.0) return texel * vColor;\n" \
	/* [rc4l] A flooded gap is lit and fogged where the plane it shows is, as DrawFloodedPlane's own polygon would be, and takes no dynamic light. */ \
	"    vec4 surfPos = vPixelPos;\n" \
	"    float surfDepth = gl_FragCoord.z;\n" \
	"    vec4 fq = fuaFloodPoint();\n" \
	"    if (fq.w > 0.5) { vec4 fc = uMVP * vec4(fq.xyz, 1.0); surfPos = vec4(fq.xyz, fc.w); surfDepth = fc.z / fc.w; wantDynLight = false; }\n" \
	"    float fogMode = vFog.a;\n" \
	"    float fogfactor = fuaFogFactor(fogMode, vLightParm.y, surfPos, uCameraPos.xyz);\n" \
	"    if (dbg == 5.0) return vec3(fogfactor);\n" \
	"    if (dbg == 6.0) return vFog.rgb;\n" \
	"    if (dbg == 7.0) return vec3(clamp(vPixelPos.w / 2000.0, 0.0, 1.0));\n" \
	"    if (dbg == 8.0) return vec3(clamp(-vLightParm.y * 20000.0, 0.0, 1.0));\n" \
	"    if (dbg == 9.0) return vec3(fogMode > 0.0 ? 1.0 : 0.0, fogMode < 0.0 ? 1.0 : 0.0, 0.0);\n" \
	/* [rc4l] Mirror clipping. Everything on the far side of the mirror plane is behind the glass and
	   must not appear in the reflection -- without this the wall the mirror hangs on renders first
	   from the reflected camera and fills the whole reflection. w == 0 means no mirror is active. */ \
	"    if (uClipPlane.w != 0.0 && dot(vPixelPos.xyz, uClipPlane.xyz) + uClipPlane.w < 0.0) discard;\n" \
	"    vec3 color = fuaSectorLight(vColor, vLightParm.x, fogMode, fogfactor, surfDepth);\n" \
	/* Dynamic lights add to the light COLOUR before the texture is modulated, exactly as
	   getLightColor does -- adding after would light the black parts of a texture too. */ \
	/* [rc4l] Brightmaps add to the LIGHT, not to the finished fragment.
	   main.fp calls ProcessLight(color) while `color` is still the light colour -- the texel is
	   multiplied in afterwards -- so a white brightmap texel means "light this texel fully", and the
	   surface keeps its own colour. Adding the same white to texel * color instead means "make this
	   texel white", and a lava floor came out as a washed-out pink slab: luminance 41 -> 162 at the
	   same camera. Right value, right texture, wrong point in the pipeline. */ \
	/* [rc4l] Under bindless the brightmap is the array's at its own slot, as the texel is, since one binding cannot carry every material's. */ \
	"    vec2 bmUV = fuaFloodUV(vUV);\n" \
	"    vec3 bm = (uSkyColor.w > 0.5 && vLightIndex > 0) ? (vBrightSlot > 0 ? texture(uMaterials[nonuniformEXT(vBrightSlot)], bmUV).rgb : vec3(0.0)) : texture(uBrightmap, bmUV).rgb;\n" \
	"    color = min(color + bm, vec3(1.0));\n" \
	/* [rc4l] Dynamic lights are optional, because a SHADED DECAL must not take them.
	   Its colour is its own -- a scorch mark is black -- and that colour arrives folded into the
	   vertex light, so a black decal has vColor 0 and the dynamic light term is then the ONLY thing
	   left in it. Firing a weapon lit every burn mark in the room with the muzzle flash's colour.
	   GL never has this problem: it applies the decal's colour as an OBJECT COLOUR that multiplies
	   the finished fragment, and zero times anything is still zero. */ \
	/* [rc4l] A sprite or model takes the light GL worked out for its actor, added after the sector light as main.fp adds uDynLightColor, and never the per-light loop. */ \
	"    if (vActorLight.w > 0.5) { if (wantDynLight) color = clamp(color + vActorLight.rgb, 0.0, 1.4); }\n" \
	"    else if (wantDynLight) color = fuaDynLight(color);\n" \
	"    vec3 frag = texel * color;\n" \
	"    return fuaColouredFog(frag, vFog.rgb, fogMode, fogfactor);\n" \
	"}\n" \
	"vec3 fuaShade(vec3 texel) { return fuaShadeEx(texel, true); }\n"

// [rc4l] Opaque: no discard, so early-Z survives and overdrawn fragments are never shaded.
static const char *kScenePSOpaque =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    outColor = vec4(fuaShade(fuaTexel(vUV).rgb), 1.0);\n"
	"}\n";

static const char *kScenePS =
	"#version 450\n"
	FUA_LIGHT_GLSL
	"void main() {\n"
	"    vec4 t = fuaTexel(vUV);\n"
	/* [rc4l] GL's solid pass tests no alpha, so an upper, lower or one-sided middle shows its see-through texels in their own colour. */
	"    if (t.a < 0.5 && vPlain < 0.5) discard;\n"
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
	"    vec4 t = fuaTexel(vUV);\n"
	/* [rc4l] GL's solid pass tests no alpha, so an upper, lower or one-sided middle shows its see-through texels in their own colour. */
	"    if (t.a < 0.5 && vPlain < 0.5) discard;\n"
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
	"    vec4 t = fuaTexel(vUV);\n"
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
	"    float a = fuaTexel(vUV).r * vLightParm.z;\n"
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

// [rc4l] The player's lens as GL last rendered it, GL's own default until a frame says otherwise.
static float g_lensFov = 90.f, g_lensRatio = 1.333333f;
void SetMainViewLens(float fovDegrees, float fovRatio) { g_lensFov = fovDegrees; g_lensRatio = fovRatio; }

static void BuildMVP(float *m)
{
	zx::hwrender::BackendCamera c;
	// [rc4l] GL's vertical field of view, derived as SetProjection derives it, where a constant 74 degrees was half a percent off and never zoomed.
	c.fovY = (g_fovOverride > 0.f) ? g_fovOverride : ::hwrender::ComputeSceneFovY(g_lensFov, g_lensRatio) * 3.14159265f / 180.0f;
	// [rc4l] From the swapchain, not a constant. A hard-coded 16:10 meant the backend framed the
	// world differently from the engine window, so a screenshot pair could never be compared
	// pixel-for-pixel -- the same screen position was a different part of the level in each.
	c.aspect = 16.0f / 10.0f;
	if (g_aspectOverride > 0.f) c.aspect = g_aspectOverride;
	else if (auto *swap = GetSwapChain())
	{
		const auto &sd = swap->GetDesc();
		if (sd.Height > 0) c.aspect = (float)sd.Width / (float)sd.Height;
	}
	c.x = FIXED2FLOAT(viewx); c.y = FIXED2FLOAT(viewy); c.z = FIXED2FLOAT(viewz);
	c.yaw = (float)(viewangle >> ANGLETOFINESHIFT) * 2.0f * 3.14159265f / 8192.0f;
	double radPitch = bam2rad((angle_t)viewpitch);
	if (radPitch > PI) radPitch -= 2 * PI;
	// Not clamp(): inside this namespace that resolves to the Fixed-point overload.
	if (radPitch < -PI / 2) radPitch = -PI / 2;
	if (radPitch >  PI / 2) radPitch =  PI / 2;
	c.pitch = (float)radPitch;
	// [rc4l] GL draws the world 1.2 times taller than square at every pitch, and this drew it square, so every wall above or below the eye sat off GL's by a fifth.
	c.pixelStretch = glset.pixelstretch;
	zx::hwrender::ComputeBackendMVP(c, m);
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

// [rc4l] A skybox is six textures on a cube, not one on a dome.
//
// GLDEFS resolves a map's sky name to an FSkyBox, which carries six FTextures and no pixels of its
// own. This backend detected one and drew it as an ordinary dome anyway, so a six-sided sky came out
// as one of its faces smeared around a cylinder -- it has been saying "[SKYBOX -- not implemented]"
// per sky since the sky path was written. See docs/skybox-scope.md.
//
// Each face is its own material, so each needs its own binding and its own draw, exactly as a world
// batch does. They share one vertex buffer; a face is a range in it.
struct SkyBoxFace
{
	int first, count;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
};
static TArray<SkyBoxFace> g_skyBoxFaces;
static Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_skyBoxVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyBoxPSO;
static bool g_skyIsBox = false;

// [rc4l] ...and a third kind of sky: one that is a PLACE.
//
// ZDoom's other skybox is a portal. An ASkyViewpoint actor sits in a room built to look like the
// horizon, and every sky surface shows the world as seen from it -- so the sky is a rendered scene,
// not a texture. Maps that use one set no sky texture at all, and this backend was falling back to
// the null placeholder and drawing a blue-and-white checkerboard. Proved by turning GL's own support
// off: `gl_noskyboxes 1` makes GL draw exactly the checkerboard this did.
//
// Rendering it is the mirror pass again -- a second camera, an offscreen target, the same DrawWorld.
// What this backend can do that GL cannot is skip the stencils: sky is drawn FIRST here, full-screen
// and depth-off, with the world painted over it, and a skybox is at infinity, so its image simply IS
// that background.
static Diligent::RefCntAutoPtr<Diligent::ITexture>       g_skyPortalColor, g_skyPortalDepth;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyPortalPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skyPortalSRB;
static int  g_skyPortalW = 0, g_skyPortalH = 0;
static bool g_skyPortalActive = false;
// [rc4l] True while the portal is rendering its own view.
//
// It does NOT suppress the sky inside that render, and suppressing it was tried: a skybox room
// draws a sky of its own, and that sky is PART OF THE SKYBOX IMAGE -- the red cloudscape around
// the city on wdiex02 is exactly that. Without it the city sits in a black surround.
static bool g_inSkyPortal = false;
static bool g_inCameraPass = false;   // [rc4l] Drawing a camera texture, whose sky quads are its own walk's.
static int g_lineMirror = -1;   // [rc4l] The line mirror whose reflection is being drawn, the one pass that shows its viewer, or -1.
// Where the portal camera stood, for the reach test in the world draw.
static float g_skyPortalCamX = 0.f, g_skyPortalCamY = 0.f;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skySurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skySurfSRB;
static int g_skyDraws = 0;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_skySurfVB;
static unsigned int g_skySurfVBCapacity = 0;   // this level's sky is a portal
// [rc4l] The stacked view being rendered (its slot), else -1, and the pipeline that paints a stacked plane from its view.
static int g_stackPassSlot = -1;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_stackSurfPSO;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_stackSurfVB;
static unsigned int g_stackSurfVBCapacity = 0;

static const char *kSkyVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in vec4 aSkyColor;\n"
	"layout(location = 3) in float aSkyAlpha;\n"
	FUA_CONSTANTS_GLSL
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

// [rc4l] One creator, two pipelines. The dome WRAPS horizontally -- a cylinder has to, the texture
// meets itself -- and a box face must CLAMP both ways or its edge pixels bleed across the seam,
// which is what GL asks for with CLAMP_XY. Everything else about the two is identical.
static bool CreateSkyPipeline(Diligent::TEXTURE_ADDRESS_MODE addrU, const char *name,
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> &outPSO)
{
	if (outPSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua sky VS", kSkyVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua sky PS", kSkyPS);
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
	// [rc4l] Not static: a static array is filled on the first call only, so the skybox's clamp took the dome's wrap and blended each face's edge with its opposite one.
	Diligent::SamplerDesc samp;
	// [rc4l] The engine's filter, as GL binds a sky texture with, where a fixed linear one smoothed every sky GL draws crisp.
	FillSamplerFromEngine(samp);
	samp.AddressU = addrU;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;   // no wrap top-to-bottom
	const Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
	};

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = name;
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
	dev->CreateGraphicsPipelineState(pci, &outPSO);
	if (!outPSO) return false;
	BindSceneConstants(outPSO);
	return true;
}

static bool EnsureSkyPipeline()
{
	if (g_skyPSO && g_skySRB) return true;
	if (!CreateSkyPipeline(Diligent::TEXTURE_ADDRESS_WRAP, "fua sky PSO", g_skyPSO)) return false;
	if (!g_skySRB) g_skyPSO->CreateShaderResourceBinding(&g_skySRB, true);
	// RefCntAutoPtr has several conversions, so `!= NULL` is ambiguous; ask it directly.
	return g_skySRB.RawPtr() != nullptr;
}

// The box gets its own pipeline for the sampler, and a binding PER FACE rather than one shared.
// [rc4l] The cube, transcribed from GL's RenderBox.
//
// Coordinates are GL's own: +/-128 with Y up, which is the space the sky vertex shader already works
// in -- it adds uCameraPos and rotates (x, z) about Y by the same (-180 + scroll) angle GL puts on
// its model matrix. So the box needs no new transform and no shader change; useTex 1 makes the pixel
// shader take the texture, which is what a face is.
//
// GL draws each face as a four-vertex strip and this pipeline is a triangle list, so each becomes
// (0,1,2) and (2,1,3). Winding does not matter: the sky pipeline culls nothing.
static void PushSkyQuad(TArray<SkyVertexData> &v,
	const float px[4], const float py[4], const float pz[4], const float uu[4], const float vv[4])
{
	static const int order[6] = { 0, 1, 2, 2, 1, 3 };
	for (int i = 0; i < 6; i++)
	{
		const int k = order[i];
		SkyVertexData d;
		d.x = px[k]; d.y = py[k]; d.z = pz[k];
		d.u = uu[k]; d.v = vv[k];
		d.r = d.g = d.b = 1.f;
		d.useTex = 1.f;    // textured and opaque -- see kSkyPS
		d.alpha  = 1.f;
		v.Push(d);
	}
}

static void BuildSkyBox(FSkyBox *sb)
{
	g_skyBoxFaces.Clear();
	g_skyBoxVB.Release();
	if (sb == NULL) return;

	TArray<SkyVertexData> verts;
	struct Pending { int first, count; FTexture *tex; };
	TArray<Pending> pending;

	// [rc4l] Two forms, told apart exactly as RenderBox tells them apart.
	//
	// With six faces each wall is its own texture. With three, faces[0] is ONE texture wrapped right
	// around the four walls -- its U runs 0, .25, .5, .75, 1 -- and the top and bottom move down to
	// faces[1] and faces[2]. Getting this wrong shows up as three of the four walls being the same
	// picture, which looks plausible enough to miss.
	const bool sixFace = (sb->faces[5] != NULL);
	const int  topIdx  = sixFace ? 4 : 1;
	const int  botIdx  = sixFace ? 5 : 2;

	const float T = 128.f;
	if (sixFace)
	{
		//                      north              east               south              west
		const float qx[4][4] = {{ T,-T, T,-T}, {-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}};
		const float qy[4][4] = {{ T, T,-T,-T}, { T, T,-T,-T}, { T, T,-T,-T}, { T, T,-T,-T}};
		const float qz[4][4] = {{-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}, { T,-T, T,-T}};
		const float qu[4]    = { 0.f, 1.f, 0.f, 1.f };
		const float qv[4]    = { 0.f, 0.f, 1.f, 1.f };
		for (int f = 0; f < 4; f++)
		{
			const int first = (int)verts.Size();
			PushSkyQuad(verts, qx[f], qy[f], qz[f], qu, qv);
			Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[f];
			pending.Push(pd);
		}
	}
	else
	{
		// One texture around all four walls: four quads sharing a material, so ONE draw.
		const float wx[4][4] = {{ T,-T, T,-T}, {-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}};
		const float wy[4]    = { T, T,-T,-T };
		const float wz[4][4] = {{-T,-T,-T,-T}, {-T, T,-T, T}, { T, T, T, T}, { T,-T, T,-T}};
		const int first = (int)verts.Size();
		for (int f = 0; f < 4; f++)
		{
			const float u0 = f * 0.25f, u1 = u0 + 0.25f;
			const float uu[4] = { u0, u1, u0, u1 };
			const float vv[4] = { 0.f, 0.f, 1.f, 1.f };
			PushSkyQuad(verts, wx[f], wy, wz[f], uu, vv);
		}
		Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[0];
		pending.Push(pd);
	}

	// Top. fliptop swaps its V, which is how a skybox author turns the ceiling the right way up.
	{
		const float tx[4] = { T, -T,  T, -T };
		const float ty[4] = { T,  T,  T,  T };
		const float tz[4] = { -T, -T, T,  T };
		const float ftop  = sb->fliptop ? 1.f : 0.f;
		const float tu[4] = { 0.f, 1.f, 0.f, 1.f };
		const float tv[4] = { ftop, ftop, 1.f - ftop, 1.f - ftop };
		const int first = (int)verts.Size();
		PushSkyQuad(verts, tx, ty, tz, tu, tv);
		Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[topIdx];
		pending.Push(pd);
	}
	// Bottom.
	{
		const float bx[4] = { T, -T,  T, -T };
		const float by[4] = { -T, -T, -T, -T };
		const float bz[4] = { -T, -T,  T,  T };
		const float bu[4] = { 0.f, 1.f, 0.f, 1.f };
		const float bv[4] = { 0.f, 0.f, 1.f, 1.f };
		const int first = (int)verts.Size();
		PushSkyQuad(verts, bx, by, bz, bu, bv);
		Pending pd; pd.first = first; pd.count = (int)verts.Size() - first; pd.tex = sb->faces[botIdx];
		pending.Push(pd);
	}

	if (verts.Size() == 0) return;

	Diligent::BufferDesc bd;
	bd.Name = "fua skybox VB";
	bd.Size = (Diligent::Uint64)verts.Size() * sizeof(SkyVertexData);
	bd.Usage = Diligent::USAGE_IMMUTABLE;
	bd.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	Diligent::BufferData bdata;
	bdata.pData = &verts[0];
	bdata.DataSize = bd.Size;
	GetDevice()->CreateBuffer(bd, &bdata, &g_skyBoxVB);
	if (!g_skyBoxVB) return;

	// A binding per face, from the same material path the world uses -- a face that cannot be
	// resolved falls back to white rather than dropping the face and leaving a hole.
	for (unsigned i = 0; i < pending.Size(); i++)
	{
		SkyBoxFace face;
		face.first = pending[i].first;
		face.count = pending[i].count;
		g_skyBoxPSO->CreateShaderResourceBinding(&face.srb, true);
		if (!face.srb) continue;
		FMaterial *fm = pending[i].tex ? FMaterial::ValidateTexture(pending[i].tex, false) : NULL;
		if (auto *v = face.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(GetMaterialSRV(fm, 0));
		g_skyBoxFaces.Push(face);
	}
}

static bool EnsureSkyBoxPipeline()
{
	return CreateSkyPipeline(Diligent::TEXTURE_ADDRESS_CLAMP, "fua skybox PSO", g_skyBoxPSO);
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
	// [rc4l] A skybox is a CUBE, and until now this drew it as a dome anyway.
	//
	// GLDEFS resolves the map's sky to an FSkyBox, which is six textures and no pixels of its own --
	// so the dome sampled one face and wrapped it around a cylinder. The marker that used to print
	// here said as much on every such sky.
	const bool isBox = (mat->tex != NULL && mat->tex->gl_info.bSkybox);
	if (isBox)
	{
		FSkyBox *sb = static_cast<FSkyBox *>(mat->tex);
		if (!EnsureSkyBoxPipeline()) { g_skyBuiltValid = false; return; }
		BuildSkyBox(sb);
		if (g_skyBoxFaces.Size() == 0) { g_skyBuiltValid = false; return; }
		if (!sameShape)
		{
			Printf("vulkan sky: %s  [SKYBOX, %s: %s %s %s %s %s %s]\n",
				(mat->tex->Name != NULL) ? mat->tex->Name : "?",
				(sb->faces[5] != NULL) ? "six faces" : "wrapped",
				(sb->faces[0] && sb->faces[0]->Name) ? sb->faces[0]->Name : "-",
				(sb->faces[1] && sb->faces[1]->Name) ? sb->faces[1]->Name : "-",
				(sb->faces[2] && sb->faces[2]->Name) ? sb->faces[2]->Name : "-",
				(sb->faces[3] && sb->faces[3]->Name) ? sb->faces[3]->Name : "-",
				(sb->faces[4] && sb->faces[4]->Name) ? sb->faces[4]->Name : "-",
				(sb->faces[5] && sb->faces[5]->Name) ? sb->faces[5]->Name : "-");
		}
		g_skyIsBox = true;
		g_skyMaterial = mat;
		g_skyBuiltValid = true;
		return;
	}

	g_skyIsBox = false;
	if (!sameShape)
	{
		Printf("vulkan sky: %s %dx%d skyoffset %d  cap %d,%d,%d / %d,%d,%d\n",
			(mat->tex != NULL && mat->tex->Name != NULL) ? mat->tex->Name : "?",
			mat->TextureWidth(), mat->TextureHeight(), g_skyTexOffset,
			g_skyCapColor[0].r, g_skyCapColor[0].g, g_skyCapColor[0].b,
			g_skyCapColor[1].r, g_skyCapColor[1].g, g_skyCapColor[1].b);
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
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua sky fade VS", kSkyFadeVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua sky fade PS", kSkyFadePS);
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

// Defined below: the world pass, reused verbatim by the sky portal exactly as the mirror pass
// reuses it.
static void DrawWorld(Diligent::IDeviceContext *ctx);

// [rc4l] Which skybox this level takes its sky from, or NULL if the sky is an ordinary one.
//
// GetSkyBox does the DefaultSkybox fallback and honours the per-sector SECF_NO*SKYBOX flags, so
// ask it rather than reading the sector fields by hand. ONE skybox per frame, from the camera
// sector: sectors may each name their own, and rendering one per sector would multiply a full
// scene render by the count -- the shape that had the driver kill this process over the mirror
// pass. See docs/tdr-crash-after-map-loads.md.
static ASkyViewpoint *CurrentSkyViewpoint()
{
	if (!fua_dg_skyportal) return NULL;
	// [rc4l] The viewpoint the VISIBLE SKY SURFACES name, first. GL renders one portal per unique
	// skybox and stencils each through the walls that name it; this backend renders one image, so
	// it has to be the one the sky in view actually belongs to. Falling back to the sector the
	// player stands in gives the right city seen from the wrong room on any map with more than
	// one skybox -- small and off-centre inside a hard-edged rectangle.
	ASkyViewpoint *fromWalls = (ASkyViewpoint *)zx::levelmesh::SkyQuadViewpoint();
	if (fromWalls != NULL) return fromWalls;
	sector_t *sec = (consoleplayer >= 0 && players[consoleplayer].camera != NULL)
		? players[consoleplayer].camera->Sector : NULL;
	// [rc4l] A stacked ceiling's "skybox" is the stack's anchor, as GLWall::SkyPlane knows, and a sky rendered from it is the town seen from a rooftop.
	ASkyViewpoint *sb = (sec != NULL && sec->portals[sector_t::ceiling] == NULL) ? sec->GetSkyBox(sector_t::ceiling) : NULL;
	if (sb == NULL) sb = level.DefaultSkybox;
	return sb;
}

// A full-screen triangle built from the vertex index -- no buffer, no input layout.
static const char *kSkyPortalVS =
	"#version 450\n"
	"layout(location = 0) out vec2 vUV;\n"
	"void main() {\n"
	"    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));\n"
	// [rc4l] V flipped: the portal is sampled by a triangle in clip space, where +Y is DOWN, while
	// the texture it reads was rasterised with row 0 at the top. Sampling straight through hands back
	// a skybox standing on its head -- and one that then appears to swing the wrong way when you look
	// up, which reads as a broken camera rather than a flipped fetch.
	"    vUV = vec2(p.x, 1.0 - p.y);\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

static const char *kSkyPortalPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(uTex, vUV).rgb, 1.0); }\n";

// [rc4l] A surface painted from a picture of the world by its own screen position -- the sky's from the skybox's view, a stacked plane from its view -- and depth-written like any wall.
static const char *kScreenSurfVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in float aPiece;\n"
	FUA_CONSTANTS_GLSL
	// [rc4l] A sky surface may never reach BELOW the horizon, and the horizon is eye level.
	//
	// A sky sits at infinity, so the line where it meets the ground is exactly the height of
	// the eye -- which is why GL builds these quads bottomed at viewz and not at the floor.
	// Baked geometry cannot know the eye height, so the clamp happens here, where it does.
	// Without it the quad paints sky over the water in front of the far shore: a bright
	// white streak straight along the horizon, mean 44 against GL 1.8 on that row.
	"layout(location = 0) out float vUp;\n"
	"void main() {\n"
	// [rc4l] NOT clamped here. Moving the vertex to eye level and then testing the moved
	// height is a tautology -- the test can never fire, which is exactly what it did. The
	// quad keeps its real height and the fragment below the horizon is discarded instead,
	// which cuts it at the horizon rather than near it.
	"    vUp = aPos.y - uCameraPos.y;\n"
	"    gl_Position = uMVP * vec4(aPos, 1.0);\n"
	"}\n";
static const char *kScreenSurfPSHead =
	"#version 450\n"
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform sampler2D uSky;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"layout(location = 0) in float vUp;\n"
	"void main() {\n";
// [rc4l] The sky stops at the horizon, and the horizon is eye level. The vertex clamp
// puts every corner at or above it; this catches what interpolation and the
// rasteriser do in between, which is where a band of sky several rows deep was
// appearing UNDER the waterline -- the blue quad rc4l photographed.
static const char *kScreenSurfPSHorizon =
	"    if (vUp < -0.5) discard;\n";
static const char *kScreenSurfPSTail =
	// [rc4l] Sampled by SCREEN FRACTION, not by texel.
	//
	// texelFetch indexes the sky image directly, which is only right while that image is
	// exactly the size of the window. It is not: a side-by-side session gives the Vulkan
	// window its own swapchain, and the portal texture is built from whichever size was
	// current -- so every fragment past the texture edge fetched nothing and the sky came
	// out as a hard-edged RECTANGLE with the map dark around it. It reproduces on no other
	// window size, which is why an embedded capture at 1024x640 never showed it.
	//
	// A fraction is also what makes fua_dg_skyportal_scale work: a half-size portal image
	// still covers the whole screen.
	// [rc4l] Against the SKY IMAGE own size, not uScreen. uScreen is filled by whichever
	// pass wrote the constants last and carries the mirror target size on some paths, which
	// squeezed the whole sky into a fraction of the window -- the skyline sitting in a
	// hard-edged rectangle. The portal texture is built from the swapchain, so its size IS
	// the window size, and asking the sampler is self-consistent by construction.
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uSky, 0));\n"
	"    outColor = vec4(texture(uSky, uv).rgb, 1.0);\n"
	"}\n";

// [rc4l] The pipeline for such surfaces: horizon cuts them at eye level as a sky must, and texType says whether uSky is one fixed picture (the sky) or one per binding (each stacked view).
// [rc4l] An empty fragment stage, for a pass that only writes depth.
static const char *kScreenSurfDepthPS = "#version 450\nvoid main() {}\n";

static Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateScreenSurfacePSO(const char *name, bool horizon,
	Diligent::SHADER_RESOURCE_VARIABLE_TYPE texType, bool depthOnly = false)
{
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !g_cb) return pso;
	const auto &sd = swap->GetDesc();
	FString ps = kScreenSurfPSHead;
	if (horizon) ps += kScreenSurfPSHorizon;
	ps += kScreenSurfPSTail;
	if (depthOnly) ps = kScreenSurfDepthPS;
	Diligent::RefCntAutoPtr<Diligent::IShader> svs, sps;
	svs = CompileShader(Diligent::SHADER_TYPE_VERTEX, name, kScreenSurfVS);
	sps = CompileShader(Diligent::SHADER_TYPE_PIXEL, name, ps.GetChars());
	if (!svs || !sps) return pso;

	Diligent::ShaderResourceVariableDesc surfVars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uSky", texType },
		{ Diligent::SHADER_TYPE_PIXEL, "Constants", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC },
	};
	// uSky is a GLSL sampler2D -- a COMBINED image sampler. Without an immutable sampler half
	// of that binding is unfilled, and the copy CreateShaderResourceBinding makes then faults
	// inside the Vulkan loader, nowhere near the shader that caused it.
	Diligent::SamplerDesc ssamp;
	ssamp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
	ssamp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
	ssamp.MipFilter = Diligent::FILTER_TYPE_POINT;
	ssamp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	ssamp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	Diligent::ImmutableSamplerDesc surfSamplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uSky", ssamp },
	};
	Diligent::LayoutElement selems[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},
	};
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = name;
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = sd.ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = sd.DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
	// [rc4l] Depth tested AND written. Measured both ways on three vantages: writing is the
	// better of the two everywhere (the car vantage 9.4%% of pixels against 9.9%%, the alley
	// 20.1%% against 21.1%%), so the sky occluding the passes after it costs less here than
	// leaving them to draw over it.
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
	// [rc4l] LESS_EQUAL, and it is the whole thing working or not.
	//
	// A sky surface is COPLANAR with the wall it replaces -- GL draws sky INSTEAD of that
	// wall, not in front of it -- so it arrives at exactly the depth already in the buffer.
	// Under the default LESS that is a fail, every time, and the pass draws nothing at all
	// while looking perfectly correct from the outside: quads captured, runs built, draws
	// submitted, no pixels. GL uses GL_LEQUAL here for the same reason.
	pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
	// [rc4l] NO depth bias. A sky surface is usually COPLANAR with the wall it replaces --
	// GL draws sky instead of that wall, not in front of it -- so biasing these away from
	// the camera makes them lose every such contest and draw nothing at all. A bias was
	// tried when the quads were derived from the map and over-emitted; taking them from GL
	// removes the reason for it, because GL only emits the ones that belong.
	pci.GraphicsPipeline.InputLayout.LayoutElements = selems;
	pci.GraphicsPipeline.InputLayout.NumElements = 3;
	pci.PSODesc.ResourceLayout.Variables = surfVars;
	pci.PSODesc.ResourceLayout.NumVariables = 2;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = surfSamplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
	if (depthOnly)
	{
		// [rc4l] A depth-only pass reads no picture, so it declares just the constants, surfVars' second entry.
		pci.PSODesc.ResourceLayout.Variables = &surfVars[1];
		pci.PSODesc.ResourceLayout.NumVariables = 1;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 0;
		pci.GraphicsPipeline.BlendDesc.RenderTargets[0].RenderTargetWriteMask = Diligent::COLOR_MASK_NONE;
	}
	pci.pVS = svs;
	pci.pPS = sps;
	dev->CreateGraphicsPipelineState(pci, &pso);
	if (!pso) return pso;
	if (!BindSceneConstants(pso))
	{
		Printf("vulkan %s OFF: pipeline missing its constants\n", name);
		pso.Release();
	}
	return pso;
}

static bool EnsureSkyPortalResources()
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;

	// [rc4l] FULL resolution, after half was tried and looked like it.
	//
	// The reasoning for halving it was that a skybox sits at infinity and is barely magnified. That
	// is true of a sky TEXTURE and false of this: a sky portal is a view of a city, full of thin
	// bright detail, and its pixels land on screen at 1:1 like any others. Halved, the skyline was
	// visibly softer than GL's beside it -- which is what rc4l reported, in those words.
	//
	// The cost it was bounding is real (one extra scene render per frame, the shape that produced
	// the TDR in tdr-crash-after-map-loads.md), so the divisor stays reachable rather than deleted:
	// fua_dg_skyportal_scale 2 halves it again if a map ever needs the frame time back.
	const auto &sd = swap->GetDesc();
	int div = fua_dg_skyportal_scale;
	if (div < 1) div = 1;
	if (div > 4) div = 4;
	const int w = (int)sd.Width / div, h = (int)sd.Height / div;
	if (w <= 0 || h <= 0) return false;
	if (!g_skyPortalColor || g_skyPortalW != w || g_skyPortalH != h)
	{
		g_skyPortalColor.Release();
		g_skyPortalDepth.Release();
		g_skyPortalSRB.Release();
		g_skySurfPSO.Release();
		g_skySurfSRB.Release();
		if (!CreateViewTextures(w, h, "fua sky portal", g_skyPortalColor, g_skyPortalDepth)) return false;
		g_skyPortalW = w; g_skyPortalH = h;
	}

	if (!g_skyPortalPSO)
	{
		Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
		vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua sky portal VS", kSkyPortalVS);
		ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua sky portal PS", kSkyPortalPS);
		if (!vs || !ps) return false;

		Diligent::SamplerDesc samp;
		samp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
		samp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
		samp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
		samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
		samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
		Diligent::ImmutableSamplerDesc samplers[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		};
		Diligent::ShaderResourceVariableDesc vars[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = "fua sky portal PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = sd.ColorBufferFormat;
		pci.GraphicsPipeline.DSVFormat = sd.DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		// Same standing as the dome it replaces: drawn first, never against the depth buffer.
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 1;
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
		pci.pVS = vs;
		pci.pPS = ps;
		dev->CreateGraphicsPipelineState(pci, &g_skyPortalPSO);
		if (!g_skyPortalPSO) return false;
	}
	if (!g_skySurfPSO)
	{
		g_skySurfPSO = CreateScreenSurfacePSO("fua sky surface", true, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
		auto *srv = g_skyPortalColor->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
		auto *tv = g_skySurfPSO ? g_skySurfPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "uSky") : NULL;
		if (tv && srv)
		{
			tv->Set(srv);
			g_skySurfSRB.Release();
			g_skySurfPSO->CreateShaderResourceBinding(&g_skySurfSRB, true);
		}
		else if (g_skySurfPSO)
		{
			Printf("vulkan sky surfaces OFF: pipeline missing its texture\n");
			g_skySurfPSO.Release();
		}
	}
	if (!g_skyPortalSRB)
	{
		g_skyPortalPSO->CreateShaderResourceBinding(&g_skyPortalSRB, true);
		if (!g_skyPortalSRB) return false;
		if (auto *v = g_skyPortalSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
			v->Set(g_skyPortalColor->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	}
	return true;
}

// Render the world from the skybox viewpoint, then paint it as this frame background.
// [rc4l] Rendered before the frame's own world pass, exactly as the camera-texture pass is. The sky
// portal is a second view of the map, and a second view cannot be taken from inside the first: the
// world draw derives its visible set and light clusters once per call, so re-entering it hands the
// outer pass the skybox room's geometry to draw with the player's camera -- and the map disappears.
static bool RenderSkyPortal(Diligent::IDeviceContext *ctx)
{
	g_skyPortalActive = false;
	if (!fua_dg_skyportal) return false;
	ASkyViewpoint *sb = CurrentSkyViewpoint();
	if (sb == NULL) return false;
	if (!EnsureSkyPortalResources()) return false;
	if (!EnsureSkyPortalResources()) return false;

	auto *swap = GetSwapChain();
	auto *brtv = swap->GetCurrentBackBufferRTV();
	// [rc4l] The world pass renders into our own readable depth buffer, so restoring the swapchain's
	// here would hand the screen a depth buffer nothing has written to.
	Diligent::ITextureView *bdsv = EnsureSceneDepth();
	if (!bdsv) bdsv = swap->GetDepthBufferDSV();
	auto *prtv = g_skyPortalColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	auto *pdsv = g_skyPortalDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	if (!prtv || !pdsv) return false;

	const fixed_t sx = viewx, sy = viewy, sz = viewz;
	const angle_t sa = viewangle;
	float savedMVP[16];
	for (int i = 0; i < 16; i++) savedMVP[i] = g_mvp[i];

	// [rc4l] The viewpoint, interpolated, with its own angle ADDED to the camera -- which is how a
	// skybox parallaxes and rotates. The BAM interpolation is the tested one; a hand-rolled version
	// is already recorded in this tree as having made a rotating portal overshoot on right turns.
	// [rc4l] Interpolated ONLY when the viewpoint has a previous position to interpolate from.
	//
	// A skybox viewpoint that never moves may carry PrevX/Y/Z of zero -- nothing has ever
	// written them -- and then this lerps from the MAP ORIGIN to the actor, landing the camera
	// somewhere out in the middle of the level and moving it with r_TicFrac. The skybox is then
	// rendered from the wrong place, which makes the city small and off-centre, and it shifts
	// as the frame fraction changes: the "sky moves with the mouse" this started from.
	const fixed_t frac = r_TicFrac;
	const bool moved = (sb->PrevX != 0 || sb->PrevY != 0 || sb->PrevZ != 0);
	viewx = moved ? sb->PrevX + FixedMul(frac, sb->x - sb->PrevX) : sb->x;
	viewy = moved ? sb->PrevY + FixedMul(frac, sb->y - sb->PrevY) : sb->y;
	viewz = moved ? sb->PrevZ + FixedMul(frac, sb->z - sb->PrevZ) : sb->z;
	viewangle = sa + (moved ? zx::InterpolateAngleBAM(sb->PrevAngle, sb->angle, (int64_t)frac)
	                        : sb->angle);

	// Never let the eye sit inside the skybox room floor or ceiling.
	if (sb->Sector != NULL)
	{
		const fixed_t fl = sb->Sector->floorplane.ZatPoint(sb->x, sb->y);
		const fixed_t ce = sb->Sector->ceilingplane.ZatPoint(sb->x, sb->y);
		if (viewz < fl + 4 * FRACUNIT) viewz = fl + 4 * FRACUNIT;
		if (viewz > ce - 4 * FRACUNIT) viewz = ce - 4 * FRACUNIT;
	}

	// [rc4l] Where the portal camera actually stands. Recorded HERE and not after the restore a
	// few lines down, which hands back the player position -- the reach test then measured from
	// the player, excluded the skybox room along with everything else, and drew nothing at all.
	g_skyPortalCamX = FIXED2FLOAT(viewx);
	g_skyPortalCamY = FIXED2FLOAT(viewy);
	BuildMVP(g_mvp);
	viewx = sx; viewy = sy; viewz = sz; viewangle = sa;

	ctx->SetRenderTargets(1, &prtv, pdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clearCol[4] = { 0.f, 0.f, 0.f, 1.f };
	ctx->ClearRenderTarget(prtv, clearCol, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(pdsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	{
		Diligent::Viewport vp;
		vp.TopLeftX = 0; vp.TopLeftY = 0;
		vp.Width = (float)g_skyPortalW; vp.Height = (float)g_skyPortalH;
		vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
		ctx->SetViewports(1, &vp, (Diligent::Uint32)g_skyPortalW, (Diligent::Uint32)g_skyPortalH);
	}
	if (g_cb)
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
	}
	g_inSkyPortal = true;
	DrawWorld(ctx);
	g_inSkyPortal = false;

	// Hand the screen back its own target and full viewport; the frame assumes both.
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, nullptr, 0, 0);
	if (g_cb)
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int k = 0; k < 16; k++) cb[k] = g_mvp[k];
	}
	// [rc4l] The picture changes hands HERE, outside the frame's render pass, not at the draw that
	// samples it. A transition asked for mid-pass makes Diligent end the pass to place the barrier and
	// begin a new one, and the world drawn afterwards lands in a pass the frame no longer owns -- which
	// is how a working portal blit came to erase the map behind it.
	{
		Diligent::StateTransitionDesc barrier(g_skyPortalColor, Diligent::RESOURCE_STATE_UNKNOWN,
			Diligent::RESOURCE_STATE_SHADER_RESOURCE, Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
		ctx->TransitionResourceStates(1, &barrier);
	}
	g_skyPortalActive = true;
	return true;
}

// [rc4l] See dgshared.h: the portal pass's camera, for passes that apply the same reach as the world.
bool SkyPortalReach(float &camX, float &camY, float &reach)
{
	if (!g_inSkyPortal || fua_dg_skyportal_reach <= 0.f) return false;
	camX = g_skyPortalCamX;
	camY = g_skyPortalCamY;
	reach = fua_dg_skyportal_reach;
	return true;
}

// [rc4l] What the viewpoint saw, laid down where the sky would otherwise be. Depth stays open, so
// the world draws over it in the same pass -- the portal is a background, not geometry.
static bool BlitSkyPortal(Diligent::IDeviceContext *ctx)
{
	if (!g_skyPortalActive || !g_skyPortalPSO || !g_skyPortalSRB) return false;
	ctx->SetPipelineState(g_skyPortalPSO);
	ctx->CommitShaderResources(g_skyPortalSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = 3;
	dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(dr);
	return true;
}

static void DrawSky(Diligent::IDeviceContext *ctx)
{
	// [rc4l] The box is one buffer and a draw per face, because each face is its own material -- the
	// same shape as a world batch, which is one draw per binding.
	if (g_skyIsBox)
	{
		if (!g_skyBuiltValid || !g_skyBoxVB || !g_skyBoxPSO || g_skyBoxFaces.Size() == 0) return;
		Diligent::IBuffer *bvbs[] = { g_skyBoxVB };
		const Diligent::Uint64 boffsets[] = { 0 };
		ctx->SetVertexBuffers(0, 1, bvbs, boffsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
			Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
		ctx->SetPipelineState(g_skyBoxPSO);
		for (unsigned i = 0; i < g_skyBoxFaces.Size(); i++)
		{
			const SkyBoxFace &f = g_skyBoxFaces[i];
			if (!f.srb || f.count == 0) continue;
			ctx->CommitShaderResources(f.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			Diligent::DrawAttribs d;
			d.NumVertices = (Diligent::Uint32)f.count;
			d.StartVertexLocation = (Diligent::Uint32)f.first;
			d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(d);
		}
		DrawSkyFade(ctx);
		return;
	}

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
// [rc4l] What the dynamic stream costs, which no other clock covers.
//
// stat rendertimes wraps GL's own work: S: Render and S: Setup are Process and Draw. This runs in the
// BACKEND, inside All= and outside every sub-clock, which is why Sunder MAP20 read 4.8 ms a frame
// with its named parts summing to 2.
double g_dynBuildMs = 0.0;

static void BuildDynamic(Diligent::IDeviceContext *ctx)
{
	cycle_t dynClock;
	dynClock.Reset();
	dynClock.Clock();
	struct ClockStop { cycle_t &c; ~ClockStop() { c.Unclock(); g_dynBuildMs = c.TimeMS(); } } stopper = { dynClock };
	g_dynDraws = g_dynTris = 0;
	g_blendSubmits = 0;
	g_dynSlotRefused = g_dynSlotNoMaterial = g_dynSlotSeen = g_dynSlotMax = 0;
	g_dynBindless = g_dynPerMaterial = 0;
	g_dynByBlend[0] = g_dynByBlend[1] = g_dynByBlend[2] = g_dynByBlend[3] = 0;
	g_dynReady = false;

	int nverts = 0, npieces = 0;
	const FFlatVertex *src = zx::levelmesh::DynVertices(nverts);
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::DynPieces(npieces);
	if (src == NULL || nverts <= 0 || pieces == NULL || npieces <= 0) { g_dynRuns.Clear(); return; }

	static TArray<SceneVertex> vb;

	// The dynamic path's piece records, rebuilt with its vertices every frame.

	static TArray<ScenePieceData> dynPieces;
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
		// [rc4l] n log n, because this ran as a selection sort over every sprite in the frame.
		//
		// The rule is unchanged -- opaque first and grouped by material, then blended back to front --
		// and only the number of comparisons is different. The pair loop was O(n^2): a Sunder MAP10
		// arena with monsters awake puts 1258 sprites in a frame, which is 790,000 comparisons every
		// frame, each dereferencing two pieces. It was 18.5% of the profile, and it is the reason the
		// same view cost twice as much with monsters as without -- the growth is in n, not in what
		// the monsters do.
		//
		// The index breaks ties so the order is DETERMINISTIC. std::sort is not stable, and two
		// sprites at the same distance trading places between frames is a flicker -- the same trap the
		// translucent draw comparator documents one pass later.
		// `key` has static storage, which a lambda cannot capture; the pointer can.
		const float *keyp = (npieces > 0) ? &key[0] : NULL;
		std::sort(&order[0], &order[0] + npieces,
			[pieces, keyp](int ia, int ib) {
				const zx::levelmesh::MeshPiece &pa = pieces[ia];
				const zx::levelmesh::MeshPiece &pb = pieces[ib];
				const bool ta = pa.blendMode != 0, tb = pb.blendMode != 0;
				if (ta != tb) return !ta;                             // opaque before blended
				if (!ta)
				{
					if (pa.material != pb.material) return pa.material < pb.material;
					return ia < ib;
				}
				if (keyp[ia] != keyp[ib]) return keyp[ia] > keyp[ib]; // blended: far first
				return ia < ib;
			});

		vb.Clear();
		runs.Clear();
		g_dynViewerRuns = 0;
		dynPieces.Clear();
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
			//
			// MEASURED, because "1234 sprites, 1234 draw calls" reads like the whole problem and is
			// not. Reusing the sorted pass's binding between draws that want the same one, and
			// collapsing draws the sort happened to leave adjacent, took a 786-sprite frame from 786
			// submissions and 786 descriptor binds to 95 and 21 -- and moved the frame time by
			// nothing, over four alternating runs. The same scene at a ninth of the pixels went from
			// 30.98 ms to 7.98 ms. This pass is FILL bound, not submission bound, and the sprites
			// cost fragments rather than draw calls.
			//
			// So the lever is overdraw, not batching: these are drawn back-to-front with depth writes
			// off, which is what lets an additive glow show through the alpha-tested sprite in front
			// of it, and also means every overlapping layer is shaded in full. The quads themselves
			// are already trimmed to their opaque borders by FMaterial's TrimBorders, so that easy
			// win is spent.
			{
				DynRun r; r.material = p.material; r.first = vb.Size(); r.count = 0; r.blend = p.blendMode;
				r.translation = p.translation;
				r.depthBias = p.depthBias;
				r.redAlpha = p.redToAlpha;
				r.solid = p.solid;
				r.mirrorLine = p.mirrorLine;
				if (p.mirrorLine >= 0) g_dynViewerRuns++;
				r.cx = p.sortX; r.cy = p.sortY; r.cz = p.sortZ;
				r.lo = 1e30f; r.hi = -1e30f;
				runs.Push(r);
			}
			// [rc4l] A dynamic piece gets a record too, appended behind the level's own.
			//
			// The static level owns the front of the buffer and never moves; sprites and decals are
			// rebuilt every frame into the back of it. A sprite that overflows the slack draws with the
			// last record rather than reading past the end, which is a wrong colour on one sprite rather
			// than undefined behaviour on the whole frame.
			// [rc4l] The dynamic region starts after ALL of the static records, asked fresh every frame.
			//
			// This used to be a count snapshotted when the scene was last rebuilt, and AppendPiece pushes
			// records past it -- baking the room behind a door that just opened does exactly that. The
			// sprites and decals of the next frame then wrote over those appended records, so a surface
			// that had just come into the world started rendering with some sprite's material. It reads
			// as textures changing at random when you open a door or fire a shot, which is how it was
			// reported.
			unsigned int dynPieceIndex = g_scenePieceData.Size() + dynPieces.Size();
			if (dynPieceIndex >= g_pieceCapacity)
				dynPieceIndex = (g_pieceCapacity > 0) ? g_pieceCapacity - 1 : 0;
			else
			{
				ScenePieceData pd;
				FillPieceData(p, p.translation, pd, false);
				if (!fua_dg_bindless_dyn) pd.matSlot = pd.brightSlot = 0.f;   // back to the bound texture and brightmap
				dynPieces.Push(pd);
			}
			// [rc4l] A quad arrives as four CORNERS and becomes six vertices here.
			//
			// It used to arrive as six, with two of the four duplicated on the engine's side and then
			// copied again into this format -- so the duplication was paid for twice. Expanding at the
			// only place that writes SceneVertex costs the same six writes and saves the other pass.
			static const int kQuadOrder[6] = { 0, 1, 2, 2, 1, 3 };
			const unsigned outCount = p.quad ? 6 : p.range.count;
			DynRun &run = runs[runs.Size() - 1];
			for (unsigned v = 0; v < outCount; v++)
			{
				const unsigned srcIndex = p.quad ? (unsigned)kQuadOrder[v] : v;
				const SceneVertex dv = ToSceneVertex(src[p.range.offset + srcIndex], (unsigned int)dynPieceIndex);
				vb.Push(dv);
				if (dv.y < run.lo) run.lo = dv.y;
				if (dv.y > run.hi) run.hi = dv.y;
			}
			run.count += outCount;
		}
	if (vb.Size() == 0) return;

		if (!UploadGrowingVB(ctx, g_dynVB, g_dynVBCapacity, &vb[0], vb.Size(), sizeof(SceneVertex), 1024, "fua dynamic VB")) return;
		// ...and the records they index, into the back of the shared piece buffer.
		if (g_pieceBuf && dynPieces.Size() > 0 &&
		    g_scenePieceData.Size() + dynPieces.Size() <= g_pieceCapacity)
			ctx->UpdateBuffer(g_pieceBuf,
				(Diligent::Uint64)g_scenePieceData.Size() * sizeof(ScenePieceData),
				(Diligent::Uint64)dynPieces.Size() * sizeof(ScenePieceData), &dynPieces[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	if (vb.Size() == 0 || !g_dynVB) return;
	g_dynReady = true;
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
// [rc4l] The sky as depth-writing surfaces -- GL's captured quads plus the horizon lines' -- painted from the sky image by screen position.
// [rc4l] Uploads a frame's screen-sampled surfaces and binds them under pso, ready for a draw per picture; false when there is nothing to draw.
static bool BindScreenSurfaces(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const TArray<SceneVertex> &sv, Diligent::IPipelineState *pso, const char *name)
{
	if (sv.Size() < 3 || !UploadGrowingVB(ctx, vb, capacity, &sv[0], sv.Size(), sizeof(SceneVertex), 256, name)) return false;
	Diligent::IBuffer *vbs[] = { vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(pso);
	return true;
}

// [rc4l] One run of bound surfaces, painted from the picture srb carries.
static void DrawScreenSurfaceRun(Diligent::IDeviceContext *ctx, Diligent::IShaderResourceBinding *srb, unsigned int first, unsigned int count)
{
	ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = count;
	dr.StartVertexLocation = first;
	dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(dr);
}

// [rc4l] This pass's sky surfaces: GL's own sky quads for the view being drawn, plus the sky halves of the horizon lines, which come from the map.
static void GatherSkySurfaces(TArray<SceneVertex> &sv)
{
	int nverts = 0, nhorizon = 0;
	const FFlatVertex *src = zx::levelmesh::SkyQuadVertices(nverts,
		zx::levelmesh::ComputeSkyDrawList(g_inCameraPass, g_stackPassSlot));
	// [rc4l] A horizon line is built for the player's eye, so a stacked view, which stands somewhere else, takes none.
	const FFlatVertex *hsrc = (g_stackPassSlot < 0) ? zx::horizon::HorizonSkyVertices(nhorizon) : NULL;
	if (src == NULL) nverts = 0;
	if (hsrc == NULL) nhorizon = 0;
	sv.Clear();
	for (int i = 0; i < nverts + nhorizon; i++) sv.Push(ToSceneVertex((i < nverts) ? src[i] : hsrc[i - nverts], 0));
}

// [rc4l] Under a plain sky dome, the same surfaces in depth only, before the world: GL's sky walls reach to 32768 and hide whatever stands behind them, and the dome is already the colour there.
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_skyOccluderPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_skyOccluderSRB;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_skyOccluderVB;
static unsigned int g_skyOccluderVBCapacity = 0;

static void DrawSkyOccluders(Diligent::IDeviceContext *ctx)
{
	if (g_skyPortalActive) return;
	if (!g_skyOccluderPSO)
	{
		g_skyOccluderPSO = CreateScreenSurfacePSO("fua sky occluder", false, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC, true);
		if (!g_skyOccluderPSO) return;
		g_skyOccluderPSO->CreateShaderResourceBinding(&g_skyOccluderSRB, true);
	}
	if (!g_skyOccluderSRB) return;
	static TArray<SceneVertex> sv;
	GatherSkySurfaces(sv);
	if (!BindScreenSurfaces(ctx, g_skyOccluderVB, g_skyOccluderVBCapacity, sv, g_skyOccluderPSO, "fua sky occluder VB")) return;
	DrawScreenSurfaceRun(ctx, g_skyOccluderSRB, 0, sv.Size());
}

static void DrawSkySurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_skyPortalActive || !g_skySurfPSO || !g_skySurfSRB) return;

	// [rc4l] Its own buffer, rebuilt each frame, since GL clears the shared dynamic stream when it re-enters its scene builder for the skybox portal, between the walk that produces these and the draw.
	static TArray<SceneVertex> sv;
	GatherSkySurfaces(sv);
	if (!BindScreenSurfaces(ctx, g_skySurfVB, g_skySurfVBCapacity, sv, g_skySurfPSO, "fua sky surface VB")) return;
	DrawScreenSurfaceRun(ctx, g_skySurfSRB, 0, sv.Size());
	g_skyDraws++;
}
// [rc4l] The dynamic pieces that are SOLID, drawn with the world instead of in the sorted pass.
//
// GL draws an opaque model in the opaque pass, depth-writing, with culling off -- it only turns
// culling on for translucent ones, to paper over having no depth sort. This is that pass: the
// masked no-cull pipeline is already alpha-tested, already writes depth and already ignores winding,
// which is the exact combination a model wants and the reason it needs no pipeline of its own.
static void DrawSolidDyn(Diligent::IDeviceContext *ctx)
{
	if (!g_dynVB || g_dynRuns.Size() == 0 || !g_maskedNoCullPSO) return;

	Diligent::IBuffer *vbs[] = { g_dynVB };
	const Diligent::Uint64 offsets[] = { 0 };
	bool bound = false;
	for (unsigned i = 0; i < g_dynRuns.Size(); i++)
	{
		const DynRun &r = g_dynRuns[i];
		if (r.count == 0 || !r.solid || r.blend != 0 || !zx::levelmesh::ComputeDrawsMirrorViewer(r.mirrorLine, g_lineMirror)) continue;
		auto *srb = WorldSRB(g_maskedNoCullPSO.RawPtr());
		if (srb) g_dynBindless++;
		else { g_dynPerMaterial++; srb = GetMaterialSRB(g_maskedNoCullPSO.RawPtr(), r.material, r.translation); }
		if (!srb) continue;
		if (!bound)
		{
			ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
				Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
			ctx->SetPipelineState(g_maskedNoCullPSO);
			bound = true;
		}
		ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		Diligent::DrawAttribs dr;
		dr.NumVertices = r.count;
		dr.StartVertexLocation = r.first;
		dr.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(dr);
		g_dynDraws++;
		g_dynTris += r.count / 3;
	}
}

// [rc4l] How many distinct translucent plane heights split the last translucent pass, for fua_dg_blendorder.
static int g_blendPlaneHeights = 0;

static void DrawBlended(Diligent::IDeviceContext *ctx)
{
	struct BlendDraw { bool dyn; unsigned first, count; int blend, translation; const void *material;
	                   Diligent::IShaderResourceBinding *srb; float dist; bool bias, red;
	                   float cx, cy, cz;      // [rc4l] the centre too: distance alone cannot tell two
	                                          // marks on ONE spot from two marks on two walls
	                   float lo, hi; int layer; bool plane; };   // [rc4l] height span, and its place among the translucent planes
	static TArray<BlendDraw> list;
	list.Clear();

	const float cx = FIXED2FLOAT(viewx), cy = FIXED2FLOAT(viewy), cz = FIXED2FLOAT(viewz);

	for (unsigned i = 0; i < g_blendBatches.Size(); i++)
	{
		SceneBatch &b = g_batches[g_blendBatches[i]];
		// [rc4l] A blended batch with no binding is skipped whether or not bindless is on.
		//
		// Relaxing this looked harmless -- with the array carrying materials a batch needs no binding
		// of its own -- and it is how translucent geometry that the old path deliberately dropped got
		// drawn again, over the whole world, in whatever the array had at its slot.
		// [rc4l] Re-resolved first, as the opaque pass does, or a minimise dropped every translucent surface until the next upload.
		if (b.count == 0 || BatchSRB(b) == NULL) continue;
		const float dx = b.sortX - cx, dy = b.sortY - cy, dz = b.sortZ - cz;
		BlendDraw d;
		d.dyn = false; d.first = b.first; d.count = b.count; d.blend = b.blend;
		d.translation = 0; d.material = b.material; d.srb = b.srb;
		d.bias = false; d.red = false;
		d.cx = b.sortX; d.cy = b.sortY; d.cz = b.sortZ;
		d.lo = b.minY; d.hi = b.maxY;   // [rc4l] SceneVertex y is height
		d.dist = dx*dx + dy*dy + dz*dz;
		list.Push(d);
	}

	if (g_dynReady)
	{
		for (unsigned i = 0; i < g_dynRuns.Size(); i++)
		{
			const DynRun &r = g_dynRuns[i];
			if (r.count == 0 || !zx::levelmesh::ComputeDrawsMirrorViewer(r.mirrorLine, g_lineMirror)) continue;
			// [rc4l] An opaque solid is drawn with the world, not in this list. See DrawSolidDyn.
			if (r.solid && r.blend == 0) continue;
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
			auto *srb = WorldSRB(pso);
			if (srb) g_dynBindless++; else { g_dynPerMaterial++; srb = GetMaterialSRB(pso, r.material, r.translation); }
			if (!srb) continue;
			BlendDraw d;
			d.dyn = true; d.first = r.first; d.count = r.count; d.blend = r.blend;
			d.translation = r.translation; d.material = r.material; d.srb = srb;
			d.bias = r.depthBias;
			d.red = r.redAlpha;
			const float dx = r.cx - cx, dy = r.cy - cy, dz = r.cz - cz;
			d.cx = r.cx; d.cy = r.cy; d.cz = r.cz;
			d.lo = r.lo; d.hi = r.hi;
			d.dist = dx*dx + dy*dy + dz*dz;
			list.Push(d);
			g_dynDraws++;
			g_dynTris += r.count / 3;
			if (r.blend >= 0 && r.blend < 4) g_dynByBlend[r.blend]++;
		}
	}

	if (list.Size() == 0) return;

	// [rc4l] Which translucent level planes stand between each draw and the eye, as GL's sort splits on them.
	{
		static TArray<zx::hwrender::BlendSpan> spans;
		static TArray<int> layers;
		static TArray<bool> planes;
		spans.Resize(list.Size());
		layers.Resize(list.Size());
		planes.Resize(list.Size());
		for (unsigned i = 0; i < list.Size(); i++)
		{
			spans[i].lo = list[i].lo; spans[i].hi = list[i].hi; spans[i].surface = !list[i].dyn;
		}
		g_blendPlaneHeights = zx::hwrender::ComputeBlendLayers(&spans[0], (int)list.Size(), cz, &layers[0], &planes[0]);
		for (unsigned i = 0; i < list.Size(); i++) { list[i].layer = layers[i]; list[i].plane = planes[i]; }
	}

	// [rc4l] Farthest first, and DETERMINISTIC on a tie.
	//
	// std::sort is not stable, so two draws at the same distance -- a decal and the one a template
	// puts underneath it, landing at the same point -- traded places between frames and flickered
	// through each other. Falling back to the buffer offset makes equal distances resolve the same
	// way every frame.
	// [rc4l] The rule itself lives in surfaceorder_compute, where it is unit-tested.
	//
	// Three layering faults shipped from this comparator -- a scorch over its own glow, a scorch over
	// the impact flash, a decal over a sprite -- and each was fixed by aiming another epsilon at the
	// pair that happened to be on screen. None could be checked without playing the game, because a
	// picture cannot say which of two draws landed second. As a function over stated inputs it can:
	// the rule that required two distances to be EXACTLY equal, and therefore never once fired, fails
	// a test written with the real numbers off a real wall.
	std::sort(&list[0], &list[0] + list.Size(),
		[](const BlendDraw &a, const BlendDraw &b) {
			zx::hwrender::TranslucentDraw da, db;
			da.distSq = a.dist; da.blend = a.blend; da.decal = a.bias; da.first = a.first;
			da.layer = a.layer; da.plane = a.plane;
			db.distSq = b.dist; db.blend = b.blend; db.decal = b.bias; db.first = b.first;
			db.layer = b.layer; db.plane = b.plane;
			return zx::hwrender::ComputeDrawsBefore(da, db);
		});

	if (g_dumpBlendOrder && g_drawingMainView)
	{
		g_dumpBlendOrder = false;
		// [rc4l] Why static translucent batches were left out, so a pass that lost them all says which test dropped them.
		int empty = 0, unbound = 0;
		for (unsigned i = 0; i < g_blendBatches.Size(); i++)
		{
			const SceneBatch &b = g_batches[g_blendBatches[i]];
			if (b.count == 0) empty++;
			else if (b.srb == NULL) unbound++;
		}
		Printf( "%u blended batches: %d empty, %d with no binding (trans PSO %s, add PSO %s)\n", g_blendBatches.Size(),
			empty, unbound, g_transPSO ? "ok" : "NULL", g_addPSO ? "ok" : "NULL" );
		Printf( "%d translucent plane heights split the order\n", g_blendPlaneHeights );
		Printf( "translucent pass, in draw order (first drawn = furthest back):\n" );
		const unsigned from = (list.Size() > 40 && !g_dumpBlendAtZ) ? list.Size() - 40 : 0;   // [rc4l] the NEAREST 40; the far end is scenery
		for (unsigned i = from; i < list.Size(); i++)
		{
			const BlendDraw &d = list[i];
			if (g_dumpBlendAtZ && (d.lo > g_dumpBlendZ || d.hi < g_dumpBlendZ)) continue;
			Printf( "  %2u  dist %9.0f  %s  blend %d%s%s  layer %d%s  z %.0f..%.0f  verts %u at %u\n", i, d.dist,
				d.dyn ? "dyn " : "batch", d.blend,
				d.bias ? "  DECAL" : "", d.red ? " redToAlpha" : "", d.layer, d.plane ? " PLANE" : "",
				d.lo, d.hi, d.count, d.first );
		}
		Printf( "  %u draws total\n", list.Size() );
	}

	// [rc4l] The sorted order, written once into indices -- see g_blendIB.
	{
		unsigned int total = 0;
		for (unsigned i = 0; i < list.Size(); i++) total += list[i].count;
		g_blendIndices.Clear();
		if (total > 0)
		{
			g_blendIndices.Reserve(total);
			unsigned int w = 0;
			for (unsigned i = 0; i < list.Size(); i++)
			{
				const BlendDraw &d = list[i];
				for (unsigned k = 0; k < d.count; k++) g_blendIndices[w++] = (Diligent::Uint32)(d.first + k);
			}
			if (EnsureBlendIndexBuffer(GetDevice(), total))
			{
				ctx->UpdateBuffer(g_blendIB, 0, (Diligent::Uint64)total * sizeof(Diligent::Uint32),
					&g_blendIndices[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				ctx->SetIndexBuffer(g_blendIB, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			}
		}
	}
	// Where each entry's indices start, walked alongside the list below.
	unsigned int idxCursor = 0;

	Diligent::IPipelineState *bound = NULL;
	// [rc4l] What is CURRENTLY committed, so the same binding is not committed again per draw.
	//
	// The pass sorts by distance so it cannot batch by material -- but with bindless there is ONE
	// binding per pipeline for the whole level, so consecutive draws in the sorted list almost always
	// want the one already there. Committing per draw costs a descriptor bind per sprite.
	//
	// This was tried once and reverted for measuring nothing, on a scene that turned out to be fill
	// bound: 786 stacked lamps at 1920x1200, where the fragments swamped everything. On Sunder MAP10
	// with GL cut out the balance is the other way -- a ninth of the pixels still leaves 2.9 ms -- and
	// the same change is worth having. The lesson kept is that this must be measured on a CPU-bound
	// frame, because on a fill-bound one it is invisible.
	Diligent::IShaderResourceBinding *boundSRB = NULL;
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
			? BatchPSO(d.blend)
			: d.red
				? ((d.blend == 2) ? g_addRedAlphaPSO.RawPtr() : g_transRedAlphaPSO.RawPtr())
				: d.bias
					? ((d.blend == 2) ? g_addDecalPSO.RawPtr()  : g_transDecalPSO.RawPtr())
					: ((d.blend == 2) ? g_addNoCullPSO.RawPtr() : g_transNoCullPSO.RawPtr());
		if (!pso) { idxCursor += d.count; continue; }
		if (pso != bound) { ctx->SetPipelineState(pso); bound = pso; boundSRB = NULL; }
		// The pipeline is only known here -- the pass sorts by distance, not by material -- so this is
		// where the per-pipeline binding can be asked for.
		Diligent::IShaderResourceBinding *srb = WorldSRB(pso);
		if (!srb) srb = d.srb;
		if (!srb) { idxCursor += d.count; continue; }
		if (srb != boundSRB)
		{
			ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			boundSRB = srb;
		}

		// [rc4l] ...and every entry after this one that wants the same state joins the same draw.
		//
		// They do not have to be adjacent in the VERTEX buffer any more -- the index buffer already
		// holds them in sorted order, so a run is contiguous in indices by construction. Nothing is
		// reordered: the run stops at the first entry that wants a different pipeline or binding, and
		// within the run the order is the sort's.
		const unsigned int idxStart = idxCursor;
		unsigned int idxCount = d.count;
		idxCursor += d.count;
		while (i + 1 < list.Size())
		{
			const BlendDraw &n = list[i + 1];
			if ((n.dyn ? 1 : 0) != want) break;
			if (n.blend != d.blend || n.red != d.red || n.bias != d.bias) break;
			// Without bindless the binding IS the material, so it has to match too.
			if (!WorldSRB(pso) && n.srb != srb) break;
			idxCount += n.count;
			idxCursor += n.count;
			i++;
		}

		if (g_blendIB && g_blendIndices.Size() > 0)
		{
			Diligent::DrawIndexedAttribs draw;
			draw.NumIndices = idxCount;
			draw.FirstIndexLocation = idxStart;
			draw.IndexType = Diligent::VT_UINT32;
			draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->DrawIndexed(draw);
		}
		else
		{
			// No index buffer this frame: fall back to the span the entry already names.
			Diligent::DrawAttribs draw;
			draw.NumVertices = d.count;
			draw.StartVertexLocation = d.first;
			draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
			ctx->Draw(draw);
		}
		g_blendSubmits++;
	}
}

// [rc4l] Drop the pipelines and everything bound to them, so the next upload rebuilds with current
// settings. SRBs are created from a PSO and cannot outlive it.
static int g_builtCull = -1;
static int g_builtFilter = -1;

static void ReleaseScenePipelines()
{
	// [rc4l] The world bindings go first, and forgetting them is a use-after-free.
	//
	// A shader resource binding is created FROM a pipeline and does not outlive it. Releasing the
	// pipelines while g_worldSRBs still held theirs took the process down on the first re-upload
	// after this function started being called for reasons other than a filter change -- and it did
	// it inside CommitShaderResources, which reads as a driver fault.
	ReleaseWorldSRBs();
	ReleasePlaneMirrors();
	ReleaseLineMirrorPipeline();
	ReleaseStackPortals();
	for (int i = 0; i < 13; i++) g_worldPSOs[i] = NULL;
	g_bindlessReady = false;
	ReleaseBatchSRBs();
	ReleaseMaterialSRBs();
	// [rc4l] Their samplers come from the engine's filter too, so they are made again with the rest.
	ReleaseHorizonPipeline();
	ReleaseDecalPipelines();
	for (unsigned i = 0; i < g_batches.Size(); i++) g_batches[i].srb = NULL;
	g_srb.Release();
	g_srbMasked.Release();
	g_skySRB.Release();
	g_skyPSO.Release();
	// The face bindings are keyed on this level's materials, so they go with everything else that is.
	g_skyBoxFaces.Clear();
	g_skyBoxVB.Release();
	g_skyBoxPSO.Release();
	g_skyIsBox = false;
	g_skyPortalSRB.Release();
	g_skyPortalPSO.Release();
	g_skyPortalColor.Release();
	g_skyPortalDepth.Release();
	g_skyPortalW = g_skyPortalH = 0;
	g_skyFadePSO.Release();
	g_skyOccluderSRB.Release();
	g_skyOccluderPSO.Release();
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
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua 2D VS", k2DVS);
	ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua 2D PS", k2DPS);
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
	Diligent::SamplerDesc samp;
	// [rc4l] CLAMP, and no mips: the engine draws 2D with CLAMP_XY_NOMIP. Wrapping would bleed the
	// opposite edge of a font glyph into its neighbour, and a mipped status bar goes soft.
	samp.MinFilter = Diligent::FILTER_TYPE_POINT;
	samp.MagFilter = Diligent::FILTER_TYPE_POINT;
	samp.MipFilter = Diligent::FILTER_TYPE_POINT;
	samp.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	const Diligent::ImmutableSamplerDesc samplers[] = {
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

		if (!UploadGrowingVB(ctx, g_vb2D, g_vb2DCapacity, &vb[0], vb.Size(), sizeof(Vertex2D), 1024, "fua 2D VB")) return;
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
	for (unsigned i = 0; i < (unsigned)nq; i++)
	{
		const zx::hwrender::Quad2D &q = quads[i];
		const int blendMode = (q.blend >= 0 && q.blend < 3) ? q.blend : 0;
		if (blendMode != curBlend)
		{
			curBlend = blendMode;
			ctx->SetPipelineState(g_pso2D[curBlend]);
			// A pipeline change drops what was committed, so the next quad has to commit again even
			// if it wants the same material.
			curMat = (const void *)(size_t)-1;
		}
		// [rc4l] Only ask for the binding when the material actually changed.
		//
		// This is a lookup keyed on (material, translation) and it ran once per QUAD -- for a status
		// bar that is the same handful of textures over and over, and for a line of text it is the
		// same font page for every glyph.
		if (q.material != curMat || q.translation != curTrans)
		{
			auto *found = GetMaterialSRB(g_pso2D[curBlend], q.material, q.translation);
			if (!found) continue;
			ctx->CommitShaderResources(found, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			curMat = q.material;
			curTrans = q.translation;
		}

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

		// [rc4l] One draw per quad, and that is fine.
		//
		// Merging runs of quads in the same state went in alongside the binding cache above and was
		// measured afterwards: a frame's HUD is SIXTEEN quads, and the merge took it to thirteen
		// draws. The pass costing 1.2 ms was the binding lookup, not the submissions -- so the merge
		// came out again rather than stay as untested cleverness in a painter's-order path where
		// getting the run condition wrong puts the status bar behind the world.
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
	const zx::hwrender::TextureFilter f = zx::hwrender::ComputeTextureFilter((int)gl_texture_filter);
	samp.MinFilter = f.minLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MagFilter = f.magLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MipFilter = f.mipLinear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT;
	samp.MaxLOD = f.mipmapped ? 1000.0f : 0.0f;
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
	vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua scene VS", kSceneVS);
	psOpaque = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS opaque", kScenePSOpaque);
	psMasked = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS masked", kScenePS);
	// [rc4l] Ask the SHADER what it declared, before any pipeline has an opinion about it.
	// "Not in the pipeline resource list" has two very different causes -- the shader never declared
	// it, or the layout classified it somewhere else -- and only the shader can tell them apart.
	if (psMasked)
	{
		FString names;
		for (Diligent::Uint32 r = 0; r < psMasked->GetResourceCount(); r++)
		{
			Diligent::ShaderResourceDesc d;
			psMasked->GetResourceDesc(r, d);
			names += " "; names += d.Name;
			if (d.ArraySize > 1) { names += "["; names.AppendFormat("%u", d.ArraySize); names += "]"; }
		}
		g_scenePSResources = names;
		Printf("Diligent scene PS declares (%u):%s\n", psMasked->GetResourceCount(), names.GetChars());
	}
	psGBuffer = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS masked + gbuffer", kScenePSGBuffer);
	psTrans = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS translucent", kScenePSTrans);
	psRedAlpha = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua scene PS red alpha", kScenePSRedAlpha);
	if (!vs || !psOpaque || !psMasked || !psTrans || !psRedAlpha)
	{ err = "scene shader compilation failed"; return false; }

	// [rc4l] Created once and never replaced, since pipelines that outlive a level change bind it statically and a replaced one is never mapped again.
	if (!g_cb)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua scene constants";
		cbd.Size = sizeof(float) * zx::hwrender::kSceneConstantFloats;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		dev->CreateBuffer(cbd, nullptr, &g_cb);
	}
	if (!g_cb) { err = "constant buffer creation failed"; return false; }

	if (!g_slotRemapBuf)
	{
		std::vector<unsigned> identity(kMaterialSlots);
		for (unsigned i = 0; i < identity.size(); i++) identity[i] = i;
		if (!CreateStructuredBuffer("fua slot remap", sizeof(unsigned), kMaterialSlots, &identity[0], g_slotRemapBuf))
		{ err = "slot remap buffer creation failed"; return false; }
	}
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
	// [rc4l] The cluster table and its index list, sized for the largest grid a window can produce.
	//
	// Created once at a fixed capacity for the same reason the light buffer is: a buffer recreated
	// mid-frame leaves every SRB pointing at the freed one. 8192 cells covers a 4K screen at 64-pixel
	// tiles with 24 slices to spare, and the index list is sized for every light landing in a
	// generous share of them.
	if (!g_clusterBuf)
	{
		g_clusterCapacity = 16384;
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
	}

	// [rc4l] `false`, not Diligent::False -- something in the reshaped DXSDK headers defines False as
	// a macro, so the qualified name does not survive the preprocessor here.
	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},   // position
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},   // uv
		Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},   // piece index
	};

	static Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		// [rc4l] The material array is MUTABLE, which is to say it lives on the BINDING.
		//
		// It was static, and static means Diligent stores it on the pipeline and copies it into
		// every binding at the moment that binding is made -- after which it cannot be rewritten.
		// Following a texture to its next animation frame therefore meant making all thirteen
		// pipelines again, shader compiles and all, twenty times a second. That was reported from
		// play as mouse stutter and as a lowering floor that could not decide what texture it was.
		//
		// On the binding, the same update is one SetArray call on the slots that actually changed.
		{ Diligent::SHADER_TYPE_PIXEL, "uMaterials", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	// [rc4l] Per call, never static: the filter is the engine's at this rebuild, and a static array would keep the first one forever.
	Diligent::SamplerDesc samp;
	FillSamplerFromEngine(samp);
	samp.AddressU = Diligent::TEXTURE_ADDRESS_WRAP;
	samp.AddressV = Diligent::TEXTURE_ADDRESS_WRAP;
	const Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uTex", samp },
		{ Diligent::SHADER_TYPE_PIXEL, "uBrightmap", samp },
		// [rc4l] The material array needs its own, and this is not optional.
		//
		// A GLSL sampler2D is a COMBINED image sampler, so an ARRAY of them needs a sampler
		// declared for the array too. Without one the variable does not reach the pipeline's
		// resource list at all -- GetStaticVariableByName returns null and the pixel statics are
		// Constants, LightBuffer and the two cluster buffers, with no sign that a sampler array
		// was ever declared. That reads as the shader having dropped it.
		{ Diligent::SHADER_TYPE_PIXEL, "uMaterials", samp },
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
		pci.GraphicsPipeline.InputLayout.NumElements = 3;
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables =
			(Diligent::Uint32)(sizeof(vars) / sizeof(vars[0]));
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers =
			(Diligent::Uint32)(sizeof(samplers) / sizeof(samplers[0]));
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

	// [rc4l] The sprite pipelines belong in here too. They were left out when they were added and the
	// backend died on launch with "No resource is assigned to static shader variable 'Constants'" --
	// a pipeline that is created but never has its statics bound is not a working pipeline.
	Diligent::IPipelineState *psos[] = { g_scenePSO, g_maskedPSO, g_transPSO, g_addPSO,
	                                     g_maskedNoCullPSO, g_transNoCullPSO, g_addNoCullPSO,
	                                     g_maskedDecalPSO, g_transDecalPSO, g_addDecalPSO,
	                                     g_transRedAlphaPSO, g_addRedAlphaPSO, g_maskedGBufPSO };
	// [rc4l] Remembered as a list, because every later question about these thirteen -- fill the
	// material array, make one binding each, find the binding for a pipeline -- is a loop over
	// them, and a hand-written list of "the world pipelines" missed five of them once already.
	ReleaseWorldSRBs();
	for (int i = 0; i < 13; i++) g_worldPSOs[i] = psos[i];
	g_matSlotsDirty = true;
	// [rc4l] Nothing to pre-fill: the material array is mutable, so it belongs to each binding and is
	// filled when that binding is made. A pipeline can hand out as many as it likes without the array
	// ever being read from it -- which is exactly the property that was missing when it was static.
	for (int i = 0; i < 13; i++)
	{
		BindSceneConstants(psos[i]);
		// [rc4l] The light buffer is shared by every world pass, so it is STATIC and set once here.
		//
		// Reported rather than silently skipped: a `if (auto *v = GetStaticVariableByName(...))` that
		// does not find its variable binds nothing and the shader reads zeros, which shows up as
		// "dynamic lights do nothing" with no error anywhere. Diligent names an SSBO by its block
		// name, but that is worth confirming rather than assuming.
		auto *lv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightBuffer");
		if (lv) lv->Set(g_lightBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		else g_lightBindFailed = true;

		// [rc4l] The per-piece records, read by the VERTEX stage. Shared by every world pass, so it is
		// static like the others -- and it is why a new piece buffer means new pipelines: a static
		// variable cannot be re-pointed once its pipeline has handed out a binding.
		if (g_pieceBuf)
		{
			auto *pv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Pieces");
			if (pv) pv->Set(g_pieceBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
			else g_pieceBindFailed = true;
		}
		if (g_stackVisBuf)
			if (auto *sv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "StackVis"))
				sv->Set(g_stackVisBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		if (auto *rv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "SlotRemap"))
			rv->Set(g_slotRemapBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));

		// The cluster table and its index list ride along on the same reasoning: shared by every world
		// pass, set once, and reported rather than silently skipped.
		if (auto *tv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightTouchRanges"))
			tv->Set(g_lightTouchRangeBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		if (auto *tv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "LightTouchIds"))
			tv->Set(g_lightTouchIdBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		auto *cv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "ClusterTable");
		if (cv) cv->Set(g_clusterBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		else g_clusterBindFailed = true;
		auto *iv = psos[i]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "ClusterLightIndices");
		if (iv) iv->Set(g_lightIndexBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		else g_clusterBindFailed = true;
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

// [rc4l] The piece's own record, written once. See ScenePieceData.
static void FillPieceData(const zx::levelmesh::MeshPiece &p, int translation, ScenePieceData &pd, bool inMesh)
{
	pd.r = p.colorR; pd.g = p.colorG; pd.b = p.colorB;
	pd.softLight = p.softLight;
	pd.fogDensity = p.fogDensity;
	pd.alpha = p.alpha;
	pd.fogMode = (float)p.fogMode;
	// [rc4l] The material SLOT. It held a dynamic light index, then a batch index for the traced
	// mirror, and it is a material slot because that is the one of the three that both the raster and
	// the traced path want: a batch index only identifies a texture by accident, since a level has
	// more batches than materials and the mirror had to mask it to 127 and hope.
	// [rc4l] A baked piece on an animated texture keys its slots on the untranslated base, which SlotRemap maps to the frame showing, since the bake froze whichever frame it saw.
	FMaterial *base = (inMesh && p.baseTex != NULL) ? FMaterial::ValidateTexture((FTexture *)p.baseTex, false) : NULL;
	const bool animates = AnimationOf(base) >= 0;
	const void *slotMat = animates ? (const void *)base : p.material;
	pd.matSlot = (float)MaterialSlotFor(slotMat, translation);
	zx::levelmesh::ComputeUnpackRGB(p.fogColor, pd.fogR, pd.fogG, pd.fogB);
	// [rc4l] A warp texture ripples in the shader, as GL's func_warp does; the upload already holds its unwarped source.
	const FMaterial *mat = (const FMaterial *)p.material;
	const FTexture *tex = (mat != NULL) ? mat->tex : NULL;
	pd.warpStyle = (float)zx::hwrender::ComputeWarpStyle(tex != NULL ? tex->bWarped : 0);
	pd.nx = p.normX; pd.ny = p.normY; pd.nz = p.normZ;
	pd.warpSpeed = (tex != NULL) ? tex->gl_info.shaderspeed : 1.f;
	pd.floodUx = p.floodUV[0]; pd.floodUy = p.floodUV[1]; pd.floodU0 = p.floodUV[2]; pd.floodZ = p.floodZ;
	pd.floodVx = p.floodUV[3]; pd.floodVy = p.floodUV[4]; pd.floodV0 = p.floodUV[5]; pd.flood = p.flood ? 1.f : 0.f;
	pd.actorR = p.dynR; pd.actorG = p.dynG; pd.actorB = p.dynB; pd.cpuLight = p.cpuLight ? 1.f : 0.f;
	// [rc4l] An animated one always takes a brightmap slot, since another frame may have a brightmap where this one has none.
	pd.brightSlot = (animates || (tex != NULL && tex->gl_info.Brightmap != NULL)) ? (float)MaterialSlotFor(slotMat, kBrightmapTranslation) : 0.f;
	pd.stackId = inMesh ? (float)(zx::levelmesh::PieceStackId(p) + 1) : 0.f;
	pd.animates = animates ? 1.f : 0.f;
	pd.plain = p.plain ? 1.f : 0.f;
}


// [rc4l] Where each piece's vertices ended up, so a moved one can be patched instead of rebuilt.
struct VBSlot
{
	unsigned int meshOffset;   // key: the piece's range in the level mesh
	unsigned int count;
	// [rc4l] How much room the slot HAS, which is not the same as how much it is drawing.
	//
	// RetireSlot zeroes `count` when a surface goes away, and without a record of the space it left
	// behind, a surface that comes BACK could only be put on the end of the buffer -- abandoning its
	// old vertices every time. A door's upper exists when the door is shut and not when it is open,
	// several times a second, so wdiex01 leaked ~1,060 slots every twelve seconds and walked from
	// 3.25 ms to a flat 33.9 ms. Keeping the capacity lets the piece go back where it was.
	unsigned int capacity;
	unsigned int vbStart;      // where those vertices live in the scene buffer
	unsigned int batch;
	// [rc4l] Where this piece's record lives. Stable across a patch: a moved sector rewrites the
	// record in place rather than taking a new index, so the vertices never have to be renumbered.
	unsigned int pieceIndex;
	bool         blended;
};
static TArray<VBSlot> g_vbSlots;
// [rc4l] Offset -> slot, because appending breaks the sortedness a binary search needs and
// re-sorting 115,000 slots to place one new piece would defeat the point of appending.
static TMap<unsigned int, unsigned int> g_slotByOffset;


// The layout this buffer was built for. The mesh bumps its generation when a piece appears, moves,
// resizes or changes the base texture or blend mode that decides its batch -- so this comparison is
// one integer rather than a hash of 115,000 pieces recomputed every frame.
static unsigned int g_builtLayoutGen = 0;
// [rc4l] The mesh revisions this scene buffer was built at -- see RefreshMovedGeometry.
static unsigned int g_builtRebatchRev = 0, g_builtRepaintRev = 0;
static int g_geomRepaints = 0;
static int g_repaintRuns = 0;
// Repaints waiting for a frame with room for them -- see where they are drained.
static TArray<unsigned int> g_pendingRepaints;
// [rc4l] What THIS frame did, so a p99 can be attributed instead of guessed at.
static int g_fRepaints = 0, g_fRuns = 0, g_fPatchVerts = 0, g_fPatches = 0;
// [rc4l] The repaint budget is per CALL of the dynamic build, not per frame, and MEASURING says
// keep it that way.
//
// BuildDynamic runs about three times a frame, so "512" drains up to ~1,536 -- and the breakdown
// showed exactly 1,536 on a p99 frame. That looks like the spreading failing, and tightening it to a
// true per-frame 512 was tried: p99 went 4.55 -> 10.44 ms and hitches 6.9% -> 28.8%. The reason is
// that the drain only runs when the dynamic build runs, which is on the tic frames -- so a per-frame
// cap starves the queue on the seven quiet frames, the backlog crosses the flush-all threshold, and
// the work lands in BIGGER lumps than before.
//
// Real spreading needs the drain lifted out of BuildDynamic so it runs on quiet frames too. Until
// then ~1,536 a tic matches the arrival rate, which is the stable answer.   // how many UpdateBuffer calls those repaints were coalesced into
// Vertices the GPU buffer can hold, which is more than the scene currently uses -- see the slack
// at creation. g_appendedVerts is how much of that slack has been spent since the last rebuild.
static unsigned int g_vbCapacity = 0;
static int g_appendedVerts = 0, g_geomAppends = 0;
// [rc4l] WHY a piece went on the end, because the two reasons want opposite fixes: a piece the
// backend has never seen is the world growing, while a piece whose slot is the wrong size is the
// same surface churning and leaking its old vertices every time.
static int g_appendNoSlot = 0, g_appendResized = 0, g_slotRevived = 0;
// Batches the last sorted build produced -- the figure appends are measured against.
static int g_builtBatchCount = 0;

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
	// [rc4l] The rule is in surfaceorder_compute, beside the translucent one, because one of its
	// three clauses is correctness and two are batching -- see ComputePiecesBefore.
	std::sort(&order[0], &order[0] + npieces, [pieces](int a, int b) {
		zx::hwrender::ScenePiece pa, pb;
		pa.blendMode = pieces[a].blendMode; pa.material = pieces[a].material; pa.baseTex = pieces[a].baseTex;
		pb.blendMode = pieces[b].blendMode; pb.material = pieces[b].material; pb.baseTex = pieces[b].baseTex;
		return zx::hwrender::ComputePiecesBefore(pa, pb);
	});

	g_sceneVB.Clear();
	g_scenePieceData.Clear();
	g_batches.Clear();
	g_blendBatches.Clear();
	g_vbSlots.Clear();
	// [rc4l] The slot table is NOT reset here, and that is the whole point of it.
	//
	// A scene rebuild re-emits every vertex, and on a level with moving sectors it happens
	// constantly -- dbab04 rebuilds about two hundred times in a minute. Resetting the table each
	// time renumbers the slots from the new emit order, so a material that was slot 3 becomes slot
	// 7, and every binding built from the old numbering is describing a different level. The
	// symptom is the whole world wearing the wrong textures with the geometry perfectly intact.
	//
	// Keeping it means a slot number is stable for as long as the level is: MaterialSlotFor finds
	// the existing entry, the indices never move, and the array only has to be built again when a
	// genuinely new material appears. The table is reset where it belongs -- on a level change,
	// in AutoSetupForLevel, next to everything else keyed on a pointer the old level owned.
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
		if (p.material != cur || p.baseTex != curBase || p.blendMode != curBlend || p.blendMode != 0 ||
		    false)
		{
			SceneBatch b;
			b.material = p.material;
			b.first = (unsigned int)g_sceneVB.Size();
			b.count = 0;
			b.masked = MaterialIsMasked(p.material);
			b.srb = NULL;   // filled once the batch list is final
			b.mirrorSector = p.mirrorSector;
			b.mirrorCeiling = p.mirrorCeiling;
			b.mirrorSlot = -1;
			b.baseTex = p.baseTex;
			b.resolved = p.material;
			b.blend = (p.blendMode == 2) ? 2 : (p.blendMode != 0 ? 1 : 0);
			b.sortX = b.sortY = b.sortZ = 0.f;
			b.normX = p.normX; b.normY = p.normY; b.normZ = p.normZ;
			b.minX = b.minY = b.minZ =  1e30f;
			b.maxX = b.maxY = b.maxZ = -1e30f;
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
		const unsigned int pieceIdx = g_scenePieceData.Size();
		{
			ScenePieceData pd;
			FillPieceData(p, 0, pd, true);
			g_scenePieceData.Push(pd);
		}
		for (unsigned int v = 0; v < p.range.count; v++)
		{
			g_sceneVB.Push(ToSceneVertex(src[p.range.offset + v], pieceIdx));
		}
		// The batch grows to hold this piece.
		{
			SceneBatch &nbb = g_batches[g_batches.Size() - 1];
			for (unsigned int v = 0; v < p.range.count; v++)
			{
				const SceneVertex &dv = g_sceneVB[vbStart + v];
				if (dv.x < nbb.minX) nbb.minX = dv.x;
				if (dv.y < nbb.minY) nbb.minY = dv.y;
				if (dv.z < nbb.minZ) nbb.minZ = dv.z;
				if (dv.x > nbb.maxX) nbb.maxX = dv.x;
				if (dv.y > nbb.maxY) nbb.maxY = dv.y;
				if (dv.z > nbb.maxZ) nbb.maxZ = dv.z;
			}
		}
		{
			VBSlot slot;
			slot.meshOffset = p.range.offset;
			slot.count = p.range.count;
			slot.capacity = p.range.count;
			slot.vbStart = vbStart;
			slot.batch = g_batches.Size() - 1;
			slot.pieceIndex = pieceIdx;
			slot.blended = (p.blendMode != 0);
			g_vbSlots.Push(slot);
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

	// [rc4l] The layout this buffer was built for, and the slots to patch it by. Sorted by mesh
	// offset so a dirty range can be walked with a binary search rather than a scan of 60,000 pieces.
	std::sort(&g_vbSlots[0], &g_vbSlots[0] + g_vbSlots.Size(),
		[](const VBSlot &x, const VBSlot &y) { return x.meshOffset < y.meshOffset; });
	g_builtLayoutGen = zx::levelmesh::MeshLayoutGeneration();
	g_appendedVerts = 0;
	g_builtBatchCount = (int)g_batches.Size();
	g_slotByOffset.Clear();
	for (unsigned i = 0; i < g_vbSlots.Size(); i++) g_slotByOffset.Insert(g_vbSlots[i].meshOffset, i);

	Diligent::BufferDesc bd;
	bd.Name = "fua scene VB";
	// [rc4l] A quarter more room than the level needs right now.
	//
	// A piece that appears mid-game -- a seg baked the first time it is looked at, a surface re-baked
	// at a different size -- can then be APPENDED rather than forcing the whole buffer to be rebuilt
	// and re-uploaded. Without slack there is nowhere to put it and every new surface in the level
	// costs a full rebuild, which is what Sunder MAP16 was doing 21 times a tic.
	g_vbCapacity = (unsigned int)(g_sceneVB.Size() + g_sceneVB.Size() / 4 + 1024);
	bd.Size = (Diligent::Uint64)g_vbCapacity * sizeof(SceneVertex);
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
	// [rc4l] Created empty, then filled -- Diligent refuses initial data smaller than the buffer,
	// and the buffer is deliberately larger than the level so pieces can be appended into the slack.
	// It rejected this outright ("initial DataSize must be larger than the buffer size") and the
	// backend then drew nothing at all, which is at least a loud way to fail.
	g_vb.Release();
	GetDevice()->CreateBuffer(bd, nullptr, &g_vb);
	if (!g_vb) { err = "vertex buffer creation failed"; return false; }

	// [rc4l] The per-piece records, sized for this level with the same slack the vertices get.
	//
	// Appending a piece must not have to resize this: a bigger buffer is a different object, and a
	// static shader variable cannot be re-pointed once its pipeline has handed out a binding. So the
	// slack is the budget, AppendPiece refuses when it runs out, and a rebuild sizes it again.
	const bool pieceBufferKept = g_pieceBuf && g_scenePieceData.Size() <= g_pieceCapacity;
	if (!pieceBufferKept)
	{
		const unsigned int want = g_scenePieceData.Size() + g_scenePieceData.Size() / 2 + 4096;
		if (!CreateStructuredBuffer("fua scene piece data", sizeof(ScenePieceData), want, NULL, g_pieceBuf))
		{ err = "piece data buffer creation failed"; return false; }
		g_pieceCapacity = want;
		if (auto *ctx2 = GetContext())
			if (g_scenePieceData.Size() > 0)
				ctx2->UpdateBuffer(g_pieceBuf, 0,
					(Diligent::Uint64)g_scenePieceData.Size() * sizeof(ScenePieceData),
					&g_scenePieceData[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	else if (auto *ctx3 = GetContext())
	{
		// [rc4l] Same buffer, new contents, and no new pipelines.
		//
		// Rebuilding the scene is not rare: a level with moving sectors does it a couple of hundred
		// times a minute, and making thirteen pipelines again each time is a stutter you can feel with
		// the mouse. A buffer that still fits is kept, and only a bigger one costs a rebuild.
		if (g_scenePieceData.Size() > 0)
			ctx3->UpdateBuffer(g_pieceBuf, 0,
				(Diligent::Uint64)g_scenePieceData.Size() * sizeof(ScenePieceData),
				&g_scenePieceData[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	// [rc4l] The stacked views' bits follow the same rule: kept while this level's subsectors fit.
	const unsigned int visWords = (unsigned int)zx::levelmesh::ComputeStackVisWords(numsubsectors + numsides);
	const bool stackVisKept = g_stackVisBuf && visWords <= g_stackVisCapacity;
	if (!stackVisKept)
	{
		const unsigned int want = zx::hwrender::ComputeGrownCapacity(visWords, 64);
		if (!CreateStructuredBuffer("fua stacked view subsectors", sizeof(unsigned int), want, NULL, g_stackVisBuf))
		{ err = "stacked view buffer creation failed"; return false; }
		g_stackVisCapacity = want;
	}
	// A NEW buffer object means the pipelines have to be made again to bind it -- the same
	// static-resource rule that governs the material array.
	if (!pieceBufferKept || !stackVisKept)
	{
		ReleaseScenePipelines();
		FString perr;
		if (!EnsureScenePipeline(perr)) { err = perr; return false; }
	}
	if (auto *ctx = GetContext())
		ctx->UpdateBuffer(g_vb, 0, (Diligent::Uint64)g_sceneVB.Size() * sizeof(SceneVertex),
			&g_sceneVB[0], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	// [rc4l] Give every batch its own SRB now that the list is final. Sized once, so the RefCntAutoPtrs
	// never move and the raw pointers handed to SceneBatch stay valid.
	// [rc4l] NOW the table describes the level whose vertices are on the GPU, and not one moment
	// earlier.
	//
	// Setting this where the table is reset looks equivalent and is not: a rebuild can still fail
	// after that point -- "no drawable pieces" is one of several ways -- and it leaves the slot
	// table describing the NEW level while the buffer the GPU draws still holds the OLD one. Every
	// surface then samples whatever material happens to sit at its index, which is exactly what
	// dbab04 did, and only when reached by a map change rather than loaded directly.
	g_bindlessGen = zx::levelmesh::LevelGeneration();
	DirtyMaterialSlots();

	ReleaseBatchSRBs();
	for (unsigned i = 0; i < g_batches.Size(); i++) BatchSRB(g_batches[i]);
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
	// Counted over the PIECES now rather than their vertices, which is what the numbers always
	// meant -- and one record per piece instead of one per vertex is the whole point of the shrink.
	for (unsigned int i = 0; i < g_scenePieceData.Size(); i++)
	{
		if (g_scenePieceData[i].softLight >= 0.f) softPieces++;
		if (g_scenePieceData[i].fogMode != 0.f) fogPieces++;
	}
	report.Format("uploaded %d verts (%.2f MB) + %d piece records (%.2f MB), %d pieces -> %d material batches (%d translucent) "
		"[lightmode %d, fogmode %d, %d%% soft-lit, %d%% fogged]",
		g_sceneVerts, (double)g_sceneVB.Size() * sizeof(SceneVertex) / (1024.0 * 1024.0),
		(int)g_scenePieceData.Size(),
		(double)g_scenePieceData.Size() * sizeof(ScenePieceData) / (1024.0 * 1024.0),
		npieces, (int)g_batches.Size(), (int)g_blendBatches.Size(),
		glset.lightmode, (int)gl_fogmode,
		g_scenePieceData.Size() ? softPieces * 100 / (int)g_scenePieceData.Size() : 0,
		g_scenePieceData.Size() ? fogPieces * 100 / (int)g_scenePieceData.Size() : 0);
	return true;
}

static bool AppendPiece(Diligent::IDeviceContext *ctx, const zx::levelmesh::MeshPiece &p,
	const FFlatVertex *src)
{
	if (p.blendMode != 0 || p.range.count == 0) return false;
	if ((unsigned int)(g_sceneVB.Size() + p.range.count) > g_vbCapacity) return false;

	SceneBatch b;
	b.material = p.material;
	b.first = (unsigned int)g_sceneVB.Size();
	b.count = p.range.count;
	b.masked = MaterialIsMasked(p.material);
	b.baseTex = p.baseTex;
	b.resolved = p.material;
	b.blend = 0;
	b.mirrorSector = -1;
	b.mirrorCeiling = false;
	b.mirrorSlot = -1;
	b.sortX = b.sortY = b.sortZ = 0.f;
	b.normX = p.normX; b.normY = p.normY; b.normZ = p.normZ;
	b.minX = b.minY = b.minZ =  1e30f;
	b.maxX = b.maxY = b.maxZ = -1e30f;
	b.srb = NULL;
	if (BatchSRB(b) == NULL) return false;   // [rc4l] no binding, no draw -- and a rebuild will do it properly
	g_batches.Push(b);

	VBSlot slot;
	slot.meshOffset = p.range.offset;
	slot.count = p.range.count;
	slot.capacity = p.range.count;
	slot.vbStart = b.first;
	slot.batch = g_batches.Size() - 1;
	// [rc4l] An appended piece needs a record as well as vertices, and the record has to reach the
	// GPU before the vertices that index it do -- a vertex pointing at a slot nobody has written is
	// undefined shading, and it would show up as one surface out of thousands looking wrong.
	slot.pieceIndex = g_scenePieceData.Size();
	if (slot.pieceIndex >= g_pieceCapacity) return false;   // out of record space: let a rebuild size it
	{
		ScenePieceData pd;
		FillPieceData(p, 0, pd, true);
		g_scenePieceData.Push(pd);
		ctx->UpdateBuffer(g_pieceBuf, (Diligent::Uint64)slot.pieceIndex * sizeof(ScenePieceData),
			sizeof(ScenePieceData), &pd, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	slot.blended = false;

	for (unsigned int v = 0; v < p.range.count; v++)
	{
		const SceneVertex dv = ToSceneVertex(src[p.range.offset + v], slot.pieceIndex);
		SceneBatch &nb = g_batches[slot.batch];
		if (dv.x < nb.minX) nb.minX = dv.x;
		if (dv.y < nb.minY) nb.minY = dv.y;
		if (dv.z < nb.minZ) nb.minZ = dv.z;
		if (dv.x > nb.maxX) nb.maxX = dv.x;
		if (dv.y > nb.maxY) nb.maxY = dv.y;
		if (dv.z > nb.maxZ) nb.maxZ = dv.z;
		g_sceneVB.Push(dv);
	}
	ctx->UpdateBuffer(g_vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
		(Diligent::Uint64)slot.count * sizeof(SceneVertex), &g_sceneVB[slot.vbStart],
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	const unsigned int idx = g_vbSlots.Push(slot);
	g_slotByOffset.Insert(slot.meshOffset, idx);
	g_appendedVerts += (int)slot.count;
	g_geomAppends++;
	return true;
}

// [rc4l] Collapse a slot the world no longer uses, without moving anything after it.
//
// A piece that was re-baked at a different size lives somewhere else now, and its old vertices are
// still in the buffer being drawn. Compacting them out would move every slot behind them; giving
// them zero area costs one upload and the rasteriser discards them. The waste is reclaimed at the
// next full rebuild, which is what the slack budget above is for.
static void RetireSlot(Diligent::IDeviceContext *ctx, VBSlot &slot)
{
	for (unsigned int v = 0; v < slot.count; v++)
	{
		SceneVertex &dv = g_sceneVB[slot.vbStart + v];
		dv.x = dv.y = dv.z = 0.f;
	}
	ctx->UpdateBuffer(g_vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
		(Diligent::Uint64)slot.count * sizeof(SceneVertex), &g_sceneVB[slot.vbStart],
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	slot.count = 0;   // nothing here to patch again
}

// [rc4l] Put a returning surface back where it was, instead of on the end of the buffer.
//
// RetireSlot leaves the slot allocated with count 0. A piece that comes back is otherwise "new" as
// far as g_slotByOffset is concerned -- its slot exists but draws nothing -- so it was appended, and
// the space it used to occupy was abandoned for the life of the level. That is the whole of the
// wdiex01 leak: 100% of its appends were this case, none of them a genuine size change.
//
// Refuses when the piece no longer fits the room it left, or when its material has moved it to a
// different batch -- reviving into a batch that names another texture is the bug that left a pressed
// switch looking unpressed. Both fall through to the append, which is always correct.
static bool ReviveSlot(Diligent::IDeviceContext *ctx, VBSlot &slot,
	const zx::levelmesh::MeshPiece &p, const FFlatVertex *src)
{
	if (slot.count != 0 || slot.capacity < p.range.count || p.range.count == 0) return false;
	if (slot.batch >= g_batches.Size()) return false;
	if (p.material != g_batches[slot.batch].material) return false;
	if (slot.pieceIndex >= g_scenePieceData.Size()) return false;

	FillPieceData(p, 0, g_scenePieceData[slot.pieceIndex], true);
	ctx->UpdateBuffer(g_pieceBuf, (Diligent::Uint64)slot.pieceIndex * sizeof(ScenePieceData),
		sizeof(ScenePieceData), &g_scenePieceData[slot.pieceIndex],
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	SceneBatch &nb = g_batches[slot.batch];
	for (unsigned int v = 0; v < p.range.count; v++)
	{
		SceneVertex &dv = g_sceneVB[slot.vbStart + v];
		dv = ToSceneVertex(src[p.range.offset + v], slot.pieceIndex);
		if (dv.x < nb.minX) nb.minX = dv.x;
		if (dv.y < nb.minY) nb.minY = dv.y;
		if (dv.z < nb.minZ) nb.minZ = dv.z;
		if (dv.x > nb.maxX) nb.maxX = dv.x;
		if (dv.y > nb.maxY) nb.maxY = dv.y;
		if (dv.z > nb.maxZ) nb.maxZ = dv.z;
	}
	// Anything past what the piece needs stays collapsed, so the slot keeps its capacity.
	for (unsigned int v = p.range.count; v < slot.capacity; v++)
	{
		SceneVertex &dv = g_sceneVB[slot.vbStart + v];
		dv.x = dv.y = dv.z = 0.f;
	}
	ctx->UpdateBuffer(g_vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
		(Diligent::Uint64)slot.capacity * sizeof(SceneVertex), &g_sceneVB[slot.vbStart],
		Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	slot.count = p.range.count;
	slot.meshOffset = p.range.offset;
	slot.blended = (p.blendMode != 0);
	return true;
}

static void RefreshMovedGeometry(Diligent::IDeviceContext *ctx)
{
	if (!g_vb || g_sceneVB.Size() == 0) return;

	// [rc4l] Two questions, both answered by the mesh, neither of them "did a vertex move".
	//
	// A surface can change completely without moving: pressing a switch swaps a sidedef's texture,
	// and a door's light level changes the shading of everything it passes. This function used to
	// return on the dirty-vertex line below before asking anything else, so those changes arrived
	// only if they happened to coincide with geometry that moved. The switch never looked pressed.
	//
	// MeshRegisterPiece decides which of the two it was, once, for walls and floors alike -- see
	// features/surfaces/computation/surfacechange_compute.h.
	{
		const unsigned int rebatch = zx::levelmesh::MeshRebatchRevision();
		if (rebatch != g_builtRebatchRev)
		{
			// A surface that changed material or pass is in the wrong batch, and only a rebuild can
			// move it. Rare: it takes a switch, a door opening, or a sector changing its flat.
			g_builtRebatchRev = rebatch;
			FString err;
			if (BuildSceneBuffer(err))
			{
				g_geomRebuilds++;
				g_sceneVerts = (int)g_sceneVB.Size();
				// A full rebuild uploaded every piece record, so anything queued is already applied.
				// Left in place it would repaint by INDEX into a piece list the rebuild reshuffled.
				g_pendingRepaints.Clear();
				zx::levelmesh::MeshClearRepaints();
				zx::levelmesh::MeshClearDirtyRanges();
				return;
			}
		}
	}
	// A repaint is the surface in the right place with stale shading baked into its record. Since the
	// shading moved out of the vertices and into that record, this is a sixty-four byte upload each.
	{
		// [rc4l] SPREAD the repaints, because they all arrive on the same frame.
		//
		// Doom's sim runs at 35 Hz and the renderer here runs at 400. Every light effect in the level
		// changes its sector on the tic, so one frame in eight is handed ~1,500 surfaces to re-shade
		// and the other seven are handed none. Even with the uploads coalesced that is a ~3 ms lump
		// landing 35 times a second -- which is what choppy movement IS, and why a healthy median
		// frame time never showed it.
		//
		// A repaint is a shading change, not a position change: a surface carrying last frame's light
		// for another two milliseconds is not visible, whereas a surface in last frame's PLACE would
		// be. So only these are deferred, never the geometry patch below, and the queue drains within
		// a couple of frames at any sane frame rate.
		const unsigned int repaint = zx::levelmesh::MeshRepaintRevision();
		if (repaint != g_builtRepaintRev)
		{
			g_builtRepaintRev = repaint;
			const unsigned int *list = NULL; int count = 0;
			zx::levelmesh::MeshTakeRepaints(list, count);
			for (int i = 0; i < count; i++) g_pendingRepaints.Push(list[i]);
			zx::levelmesh::MeshClearRepaints();
		}
		if (g_pendingRepaints.Size() > 0)
		{
			int np = 0;
			const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(np);
			// Enough to drain a tic's worth in two or three frames. If the backlog ever runs away --
			// a level load, a frame rate collapse -- take the lot rather than let it grow without
			// bound; a one-off lump is better than shading that never catches up.
			// [rc4l] Per CALL, and that is not a slip -- see g_repaintBudget.
			const unsigned int kPerCall = 512, kFlushAll = 8192;
			unsigned int budget = (g_pendingRepaints.Size() > kFlushAll)
				? g_pendingRepaints.Size() : kPerCall;
			if (budget > g_pendingRepaints.Size()) budget = g_pendingRepaints.Size();
			const unsigned int *list = &g_pendingRepaints[0];
			const int count = (int)budget;
			// [rc4l] Fill the mirror first, upload in RUNS -- one UpdateBuffer per repaint was the
			// stutter.
			//
			// Doom's light effects run every tic, so on the frame after a tic a level with animated
			// lighting repaints a thousand-odd surfaces at once. Each one was a separate 64-byte
			// UpdateBuffer, and 1,548 driver calls do not cost 1,548 times nothing: it put a ~5 ms
			// spike on one frame in eight at 35 Hz, which reads as choppy mouse movement however
			// healthy the median looks. The frame-time MEDIAN never showed it -- that is exactly the
			// thing rc4l said the metrics were missing.
			//
			// g_scenePieceData is a full CPU mirror of the buffer, so any span of it is safe to
			// upload whether or not every entry in the span changed. That makes near-misses
			// mergeable: pieces baked together are allocated together, so a sector's surfaces land in
			// a handful of runs rather than a thousand scattered writes.
			static TArray<unsigned int> s_repaintSlots;
			s_repaintSlots.Clear();
			for (int i = 0; i < count && pieces != NULL; i++)
			{
				if ((int)list[i] >= np) continue;
				const zx::levelmesh::MeshPiece &p = pieces[list[i]];
				unsigned int *slotIdx = g_slotByOffset.CheckKey(p.range.offset);
				if (slotIdx == NULL || *slotIdx >= g_vbSlots.Size()) continue;
				const VBSlot &slot = g_vbSlots[*slotIdx];
				if (slot.count == 0 || slot.pieceIndex >= g_scenePieceData.Size()) continue;
				FillPieceData(p, 0, g_scenePieceData[slot.pieceIndex], true);
				s_repaintSlots.Push(slot.pieceIndex);
				g_geomRepaints++;
				g_fRepaints++;
			}
			if (g_pieceBuf && s_repaintSlots.Size() > 0)
			{
				std::sort(&s_repaintSlots[0], &s_repaintSlots[0] + s_repaintSlots.Size());
				// Bridging a gap re-sends entries that did not change. 32 of them is 2 KB of slack
				// against a whole driver call, which is the trade every time.
				const unsigned int kBridge = 32;
				unsigned int runStart = s_repaintSlots[0], runEnd = runStart;
				for (unsigned int i = 1; i <= s_repaintSlots.Size(); i++)
				{
					const bool end = (i == s_repaintSlots.Size());
					if (!end && s_repaintSlots[i] <= runEnd + kBridge)
					{
						if (s_repaintSlots[i] > runEnd) runEnd = s_repaintSlots[i];
						continue;
					}
					ctx->UpdateBuffer(g_pieceBuf,
						(Diligent::Uint64)runStart * sizeof(ScenePieceData),
						(Diligent::Uint64)(runEnd - runStart + 1) * sizeof(ScenePieceData),
						&g_scenePieceData[runStart],
						Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					g_repaintRuns++;
					g_fRuns++;
					if (!end) { runStart = runEnd = s_repaintSlots[i]; }
				}
			}
			// Drop what was just applied; whatever is left rides to the next frame.
			g_pendingRepaints.Delete(0, (int)budget);
		}
	}
	unsigned int lo = 0, hi = 0;
	zx::levelmesh::MeshTakeDirty(lo, hi);
	// [rc4l] Clear the itemised list HERE too, or it accumulates across every quiet frame until
	// it overflows -- and an overflowed list reports "assume everything moved", which sent this
	// straight to a full rebuild every frame while claiming the layout was stable. 0 patches, 188
	// rebuilds, and a generation that matched the one the buffer was built for.
	if (hi <= lo) { zx::levelmesh::MeshClearDirtyRanges(); g_geomUpdates = 0; return; }
	g_lastDirtyLo = lo; g_lastDirtyHi = hi;

	int srcCount = 0;
	const FFlatVertex *src = zx::levelmesh::MeshVertexData(srcCount);
	int npieces = 0;
	const zx::levelmesh::MeshPiece *pieces = zx::levelmesh::MeshPieces(npieces);
	if (src == NULL || pieces == NULL) return;

	// [rc4l] Patch what moved; rebuild only when the LAYOUT moved.
	//
	// This used to call BuildSceneBuffer unconditionally, which sorts every piece in the level and
	// re-uploads the whole vertex buffer. The comment justifying that said it "only happens on the
	// frames something actually moves" -- true on a small map, and on Sunder MAP16 that is every
	// frame, because something is always being baked, animating or lit differently. 6506 full
	// rebuilds in a couple of hundred tics, and the backend cost 4.5 ms a frame while its actual
	// drawing measured 0.23.
	//
	// Patching in place was tried before and abandoned for a real reason: a re-baked piece can change
	// vertex count, MeshStore then gives it a NEW range, and every offset recorded at upload time is
	// stale from that moment -- the shut door stayed painted across an open doorway. The answer is not
	// to give up on patching but to NOTICE: a signature over every piece's offset, size, material,
	// base texture and blend mode says whether the layout is the one this buffer was built for. When
	// it is not, the full rebuild still happens, exactly as before.
	// [rc4l] A layout change no longer means a rebuild.
	//
	// It used to: the generation moved, the whole buffer was sorted and re-uploaded, and on Sunder
	// MAP16 that happened every frame because ~21 pieces a tic are baked or re-baked at a new size.
	// Rebuilds went 6506 -> 22 by patching what moved, and the frame did not shift at all, because
	// the 22 that remained were each 17 ms.
	//
	// So the three things that can happen to a piece are handled where they happen: patched in place
	// if it is the size its slot expects, appended at the end if it is new or has outgrown its slot,
	// and collapsed to zero area if the world has stopped using it. A full rebuild is what happens
	// when the slack runs out, which also reclaims everything the collapses left behind.
	if (g_vbSlots.Size() > 0 && g_vb)
	{
		int ndirty = 0;
		const zx::levelmesh::MeshRange *dirty = zx::levelmesh::MeshTakeDirtyRanges(ndirty);
		if (dirty != NULL)
		{
			bool bailed = false;
			int patched = 0;
			for (int d = 0; d < ndirty && !bailed; d++)
			{
				const unsigned int dl = dirty[d].offset;

				// The piece that owns this range now, which may be a different one from last frame.
				const zx::levelmesh::MeshPiece *p = zx::levelmesh::MeshPieceByOffset(dl);
				unsigned int *slotIdx = g_slotByOffset.CheckKey(dl);

				if (p == NULL || p->range.count == 0)
				{
					// The world dropped it. Collapse whatever is still being drawn there.
					if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() && g_vbSlots[*slotIdx].count > 0)
						RetireSlot(ctx, g_vbSlots[*slotIdx]);
					continue;
				}

				if (p->blendMode != 0) { bailed = true; break; }   // the sorted pass owns these

				if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() &&
					g_vbSlots[*slotIdx].count == p->range.count)
				{
					const VBSlot &slot = g_vbSlots[*slotIdx];
					if (slot.blended) { bailed = true; break; }
					// [rc4l] A piece whose MATERIAL changed cannot be patched in place.
					//
					// A batch is a run of pieces that share a material and, without bindless, one
					// binding. Rewriting a piece's vertices where it lies leaves it in a batch that
					// still names the old texture, so it goes on being drawn with it -- which is why
					// pressing a switch left the switch looking unpressed. The seg re-bakes correctly
					// (side_t::SetTexture bumps fua_dirty); it is the batching that has to be redone,
					// and a rebuild is the only thing that can move a piece between batches.
					if (slot.batch < g_batches.Size() && p->material != g_batches[slot.batch].material)
						{ bailed = true; break; }
					// [rc4l] The record is rewritten in place and the index does not move.
					//
					// A door's shading changes as it moves -- its light level, and on a slope its normal --
					// so patching the vertices alone would leave the surface lit for where it used to be.
					if (slot.pieceIndex < g_scenePieceData.Size())
					{
						FillPieceData(*p, 0, g_scenePieceData[slot.pieceIndex], true);
						ctx->UpdateBuffer(g_pieceBuf,
							(Diligent::Uint64)slot.pieceIndex * sizeof(ScenePieceData), sizeof(ScenePieceData),
							&g_scenePieceData[slot.pieceIndex],
							Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					}
					for (unsigned int v = 0; v < slot.count; v++)
						g_sceneVB[slot.vbStart + v] = ToSceneVertex(src[slot.meshOffset + v], slot.pieceIndex);
					ctx->UpdateBuffer(g_vb, (Diligent::Uint64)slot.vbStart * sizeof(SceneVertex),
						(Diligent::Uint64)slot.count * sizeof(SceneVertex), &g_sceneVB[slot.vbStart],
						Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
					g_geomPatchVerts += (int)slot.count;
					g_fPatchVerts += (int)slot.count;
					patched++;
					continue;
				}

				// New, or no longer the size its slot holds: the old vertices stop being drawn and the
				// piece goes on the end.
				// A slot that is merely EMPTY can take the piece back -- see ReviveSlot.
				if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() &&
					ReviveSlot(ctx, g_vbSlots[*slotIdx], *p, src))
				{
					g_slotRevived++;
					patched++;
					continue;
				}
				if (slotIdx != NULL && *slotIdx < g_vbSlots.Size() && g_vbSlots[*slotIdx].count > 0)
				{
					g_appendResized++;
					RetireSlot(ctx, g_vbSlots[*slotIdx]);
				}
				else g_appendNoSlot++;
				if (!AppendPiece(ctx, *p, src)) { bailed = true; break; }
			}

			// [rc4l] Rebuild when the slack is spent, not when the layout changes. The collapsed slots
			// and the extra batches are both reclaimed by the sort, so this is compaction rather than
			// failure -- and it happens on the order of once per level rather than once per frame.
			// [rc4l] Compact when the BATCH LIST has bloated, not only when the buffer has.
			//
			// An appended piece gets a batch of its own, and a batch costs a material binding and a draw
			// call every frame forever. Sunder MAP16 fully baked reached 2348 batches where the sorted
			// build produces 168, and submit time went 0.23 ms to 0.54 with it. The sort is what merges
			// them back together, so the sort is worth running again once enough have piled up.
			//
			// Half again as many as the last sorted build: often enough to keep the cost near the
			// compacted figure, rare enough that a rebuild is not what the frame is doing.
			const bool batchesBloated = (g_builtBatchCount > 0) &&
				((int)g_batches.Size() > g_builtBatchCount + g_builtBatchCount / 2 + 32);
			const bool slackSpent = (g_appendedVerts > (int)(g_vbCapacity / 8)) || batchesBloated;
			if (!bailed && !slackSpent)
			{
				if (patched > 0 || g_geomAppends > 0) g_geomPatches++;
				zx::levelmesh::MeshClearDirtyRanges();
				g_builtLayoutGen = zx::levelmesh::MeshLayoutGeneration();
				g_sceneVerts = (int)g_sceneVB.Size();
				g_geomUpdates = 0;
				return;
			}
		}
	}

	// [rc4l] The layout changed -- or a translucent surface moved, or a piece resized under its own
	// offset. A full rebuild costs a sort and an upload, and is now what happens when it has to.
	FString err;
	g_geomUpdates = BuildSceneBuffer(err) ? 1 : 0;
	g_sceneVerts = (int)g_sceneVB.Size();
	g_geomRebuilds += g_geomUpdates;
	zx::levelmesh::MeshClearDirtyRanges();
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
			const float ph = FIXED2FLOAT( sec->floorplane.ZatPoint( light->x, light->y ) );
			Printf( "          floor plane under the light %.3f vs light z %.3f -> GL %s\n",
				ph, lz, ( ph > lz ) ? "DROPS it" : "keeps it" );
		}
		n++;
	}
	Printf( "fua_dg_lights: %d active, player floor %.1f\n", n, floorz );
}
// [rc4l] Collect every active dynamic light in the level into the shared storage buffer.
//
// Rebuilt every frame -- lights move, spawn and die. See fuaDynLight in the shader for why there is
// no per-surface light list at all.
// [rc4l] The ids a light is linked to, as PieceStackId names surfaces: each subsector its flood reached, and numsubsectors plus each sidedef it lights.
static void CollectLightLinks(ADynamicLight *light, std::vector<unsigned> &out)
{
	out.clear();
	for (FLightNode *n = light->touching_subsectors; n != NULL; n = n->nextTarget)
		if (n->targSubsector != NULL) out.push_back((unsigned)(n->targSubsector - subsectors));
	for (FLightNode *n = light->touching_sides; n != NULL; n = n->nextTarget)
		if (n->targLine != NULL) out.push_back((unsigned)(numsubsectors + (n->targLine - sides)));
}

static void CollectDynamicLights(Diligent::IDeviceContext *ctx)
{
	static std::vector<std::vector<unsigned> > touched;
	g_lightCount = 0;
	if (fua_dg_dynlights && g_lightBuf)
	{
		// [rc4l] Straight from the level's thinkers, not from the engine's per-surface light buffer.
		// Same filters gl_GetLight applies: dormant lights are skipped, and a zero radius means off.
		g_lightData.Clear();
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
			g_lightData.Push(FIXED2FLOAT(light->x));
			g_lightData.Push(FIXED2FLOAT(light->z));
			g_lightData.Push(FIXED2FLOAT(light->y));
			g_lightData.Push(radius);
			g_lightData.Push(r);
			g_lightData.Push(g);
			g_lightData.Push(b);
			g_lightData.Push(light->IsSubtractive() ? 1.0f : 0.0f);
			if ((size_t)g_lightCount >= touched.size()) touched.resize(g_lightCount + 1);
			CollectLightLinks(light, touched[g_lightCount]);
			g_lightCount++;
		}
		if (g_lightCount > 0)
		{
			ctx->UpdateBuffer(g_lightBuf, 0, (Diligent::Uint64)g_lightData.Size() * 4, &g_lightData[0],
				Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
			touched.resize(g_lightCount);
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
static void BuildLightClusters(Diligent::IDeviceContext *ctx)
{
	g_clusterCells = 0;
	g_clusterRefs = 0;
	g_clusterTruncated = 0;
	if (!fua_dg_clusters || !g_clusterBuf || !g_lightIndexBuf || g_lightCount <= 0) return;

	auto *swap = GetSwapChain();
	if (swap == NULL) return;
	const auto &sd = swap->GetDesc();

	// [rc4l] The slice range is the CLUSTER far plane, not the projection's.
	//
	// BuildMVP's far plane is 65536 so that nothing in a Doom map is ever clipped, and slicing that
	// range would spend every slice on distance no light reaches. Anything beyond this falls in the
	// last slice, which is correct rather than approximate: a light 8000 units away is not lighting
	// you either way.
	const zx::hwrender::ClusterGrid grid =
		zx::hwrender::ComputeGridForScreen((int)sd.Width, (int)sd.Height,
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
			zx::hwrender::ComputeLightClustersFromMVP(grid, g_mvp, lp, lp[3]);
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
void DrawHorizonPlanes(Diligent::IDeviceContext *ctx);

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
	int line;             // [rc4l] its linedef, which the viewer sprites GL drew in it are filed under
};
static TArray<MirrorSurface> g_mirrors;
static Diligent::RefCntAutoPtr<Diligent::IBuffer> g_mirrorVB;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_mirrorPSO;
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_mirrorSRB;
// [rc4l] Its traced path binds the level's materials and piece buffer on the pipeline, so a level change makes it again.
static void ReleaseLineMirrorPipeline() { g_mirrorSRB.Release(); g_mirrorPSO.Release(); }
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
		m.line = i;
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
	FUA_CONSTANTS_GLSL
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
	FUA_CONSTANTS_GLSL
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(binding = 2) uniform accelerationStructureEXT uTLAS;\n"
	"layout(std430, binding = 3) readonly buffer Verts { float vtx[]; };\n"
	"layout(binding = 4) uniform sampler2D uMaterials[" FUA_MAT_SLOTS_STR "];\n"
	"layout(binding = 5) uniform sampler2D uSky;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	"layout(location = 0) out vec4 outColor;\n"
	// [rc4l] Six floats a vertex now: position, texture coordinate, and the index of the record
	// that holds everything else. The colour and the material this ray hits come from there.
	"const uint STRIDE = 6u;\n"
	FUA_PIECEDATA_GLSL
	"uint vertPiece(uint v) { return uint(vtx[v * STRIDE + 5u] + 0.5); }\n"
	"vec3 vertColor(uint v) { return pieces[vertPiece(v)].colorLight.rgb; }\n"
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
	// [rc4l] The MATERIAL slot, shared with the raster path -- it used to be a batch index masked to
	// 127, which on any level with more than 128 batches reflected the wrong texture and could not
	// say so.
	"    uint mat = uint(pieces[vertPiece(i0)].fogAlphaMat.w + 0.5) & 511u;\n"
	"    c *= texture(uMaterials[nonuniformEXT(mat)], uv).rgb;\n"
	// Distance falls off the same way the raster path's fog does, so a far reflection reads as far.
	"    float t = rayQueryGetIntersectionTEXT(rq, true);\n"
	"    c *= clamp(1.0 - t / 4000.0, 0.15, 1.0);\n"
	"    outColor = vec4(c, 1.0);\n"
	"}\n";

// [rc4l] The planar path's surface: the reflection RenderMirrors rendered from the mirrored eye, by screen position, with no ray query so an adapter without one can still draw it.
// [rc4l] Flipped left to right, since the mirrored eye sees the room the right way round and a mirror shows it reversed.
static const char *kMirrorPlanarPS =
	"#version 450\n"
	"layout(binding = 1) uniform sampler2D uTex;\n"
	"layout(location = 0) in vec3 vWorld;\n"
	"layout(location = 1) in vec3 vNormal;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    vec2 uv = gl_FragCoord.xy / vec2(textureSize(uTex, 0));\n"
	"    outColor = vec4(texture(uTex, vec2(1.0 - uv.x, uv.y)).rgb, 1.0);\n"
	"}\n";

// [rc4l] Which shader g_mirrorPSO was built with; the traced one only where the structure exists and fua_dg_rtmirrors asks for it.
static bool g_mirrorPSOTraced = false;

static bool EnsureMirrorResources()
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;
	const int w = (int)swap->GetDesc().Width, h = (int)swap->GetDesc().Height;

	if (!g_mirrorColor || g_mirrorW != w || g_mirrorH != h)
	{
		g_mirrorColor.Release(); g_mirrorDepth.Release(); g_mirrorSRB.Release();
		if (!CreateViewTextures(w, h, "fua mirror", g_mirrorColor, g_mirrorDepth)) return false;
		g_mirrorW = w; g_mirrorH = h;
	}

	// [rc4l] A pipeline built for the other path is rebuilt, since the two shaders bind different resources.
	const bool wantTraced = g_tlas && fua_dg_rtmirrors;
	if (g_mirrorPSO && g_mirrorPSOTraced != wantTraced) { g_mirrorSRB.Release(); g_mirrorPSO.Release(); }
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

		const Diligent::ImmutableSamplerDesc samplers[] = {
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
		// The traced path reads the same per-piece records the raster path does, so a reflection can
		// never disagree with the surface it reflects about what colour or texture that surface is.
		if (g_pieceBuf)
			if (auto *v = g_mirrorPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Pieces"))
				v->Set(g_pieceBuf->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE));
		BindSceneConstants(g_mirrorPSO);
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
			for (Diligent::Uint32 mi = 0; mi < (Diligent::Uint32)kMaterialSlots; mi++)
			{
				const void *mat = (mi < g_matSlotTable.Size()) ? g_matSlotTable[mi].material : NULL;
				const int tr = (mi < g_matSlotTable.Size()) ? g_matSlotTable[mi].translation : 0;
				Diligent::IDeviceObject *obj = GetMaterialSRV(mat, tr);
				if (obj == nullptr) obj = white;
				if (mi < g_matSlotTable.Size() && mat != NULL && obj == white) fellBack++;
				v->SetArray(&obj, mi, 1);
			}
			// [rc4l] Count how many came back as the white placeholder. A reflection showing flat white
			// surfaces looks the same whether the texture failed to upload or the index is wrong, and
			// this separates the two without reading it off a screenshot.
			Printf("mirror: %u materials bound, %u fell back to white\n",
				(unsigned)g_matSlotTable.Size(), fellBack);
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
		if (g_mirrorPSOTraced)
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

// [rc4l] The scene constant block for this frame's camera; passes differ only in clip plane, target size and bindless.
static void WriteSceneConstants(float *cb, const float *clip, float targetW, float targetH, float materialSlots)
{
	zx::hwrender::SceneConstantInputs in;
	in.mvp = g_mvp;
	in.camX = FIXED2FLOAT(viewx); in.camY = FIXED2FLOAT(viewy); in.camZ = FIXED2FLOAT(viewz);
	in.lightMode = (float)(int)fua_dg_lightmode;
	in.lightCount = (float)g_lightCount;
	in.skyAngle = g_skyAngle;
	in.clusters = g_clusterCells > 0;
	in.clip = clip;
	in.targetW = targetW; in.targetH = targetH;
	in.skyXScale = g_skyXScale; in.skyVScale = g_skyVScale;
	in.skyCapR = g_skyCapColor[0].r; in.skyCapG = g_skyCapColor[0].g; in.skyCapB = g_skyCapColor[0].b;
	in.materialSlots = materialSlots;
	in.frameMS = I_MSTime();
	in.stackSlot = g_stackVisReady ? g_stackPassSlot : -1;
	zx::hwrender::ComputeSceneConstants(in, cb);
}

static void DrawMirrorSurface(Diligent::IDeviceContext *ctx, unsigned index)
{
	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		WriteSceneConstants(&cb[0], nullptr, (float)g_mirrorW, (float)g_mirrorH, 0.f);
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

// [rc4l] The world from another eye, at the window's size, and the binding that paints a surface from it: a reflective plane's reflection, a stacked view.
struct ViewTarget
{
	Diligent::RefCntAutoPtr<Diligent::ITexture> color, depth;
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
	int w, h;
	ViewTarget() : w(0), h(0) {}
	void Release() { srb.Release(); depth.Release(); color.Release(); w = h = 0; }
};

// [rc4l] Makes a target's pictures at the window's size, and its binding on pso's texture variable, whenever it has none at that size.
static bool EnsureViewTarget(ViewTarget &t, Diligent::IPipelineState *pso, const char *var, const char *name)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || !pso) return false;
	const auto &sd = swap->GetDesc();
	if (t.color && t.depth && t.srb && t.w == (int)sd.Width && t.h == (int)sd.Height) return true;
	t.Release();
	if (!CreateViewTextures((int)sd.Width, (int)sd.Height, name, t.color, t.depth)) return false;
	pso->CreateShaderResourceBinding(&t.srb, true);
	if (!t.srb) return false;
	if (auto *v = t.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, var))
		v->Set(t.color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	t.w = (int)sd.Width; t.h = (int)sd.Height;
	return true;
}

// [rc4l] The world into a target, from the eye now in viewx/viewy/viewz/viewpitch and through g_clipPlane.
static void RenderWorldInto(Diligent::IDeviceContext *ctx, ViewTarget &t)
{
	BuildMVP(g_mvp);
	auto *rtv = t.color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	auto *dsv = t.depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	ctx->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
	ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	DrawWorld(ctx);
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
static const int kMaxPlaneMirrors = 4;
static ViewTarget g_pm[kMaxPlaneMirrors];
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pmPSO;
// [rc4l] Reflections rendered this frame.
static int g_pmUsed = 0;

static void ReleasePlaneMirrors()
{
	for (int k = 0; k < kMaxPlaneMirrors; k++) g_pm[k].Release();
	g_pmPSO.Release();
	g_pmUsed = 0;
}

static bool EnsurePlaneMirrorResources()
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
	for (int k = 0; k < g_pmUsed; k++)
		if (!EnsureViewTarget(g_pm[k], g_pmPSO, "uReflection", "fua plane mirror")) return false;
	return true;
}

// [rc4l] Before the frame: pick the reflective planes the eye faces and can see, and render each one's reflection.
static void RenderPlaneMirrors(Diligent::IDeviceContext *ctx)
{
	g_pmUsed = 0;
	for (unsigned i = 0; i < g_blendBatches.Size(); i++) g_batches[g_blendBatches[i]].mirrorSlot = -1;
	int maxSlots = fua_dg_planemirrors;
	if (maxSlots > kMaxPlaneMirrors) maxSlots = kMaxPlaneMirrors;
	if (maxSlots <= 0 || sectors == NULL || !g_vb) return;

	float mvp[16];
	BuildMVP(mvp);
	const float vz = FIXED2FLOAT(viewz);
	zx::hwrender::MirrorPlane slots[kMaxPlaneMirrors];
	for (unsigned i = 0; i < g_blendBatches.Size(); i++)
	{
		SceneBatch &b = g_batches[g_blendBatches[i]];
		if (b.mirrorSector < 0 || b.mirrorSector >= numsectors || b.count == 0) continue;
		const sector_t &s = sectors[b.mirrorSector];
		const float z = FIXED2FLOAT((b.mirrorCeiling ? s.ceilingplane : s.floorplane).ZatPoint(viewx, viewy));
		if (!zx::hwrender::ComputePlaneMirrorFaces(b.mirrorCeiling, z, vz)) continue;
		const float mn[3] = { b.minX, b.minY, b.minZ }, mx[3] = { b.maxX, b.maxY, b.maxZ };
		if (!zx::hwrender::ComputeBoxMayBeVisible(mvp, mn, mx)) continue;
		zx::hwrender::MirrorPlane p;
		p.ceiling = b.mirrorCeiling;
		p.z = z;
		b.mirrorSlot = zx::hwrender::ComputePlaneMirrorSlot(slots, g_pmUsed, maxSlots, p);
	}
	if (g_pmUsed == 0) return;
	if (!EnsurePlaneMirrorResources()) { g_pmUsed = 0; return; }

	const fixed_t sz = viewz;
	const int sp = viewpitch;
	float savedMVP[16];
	for (int k = 0; k < 16; k++) savedMVP[k] = g_mvp[k];
	const bool savedGBuf = g_gbufBound;
	g_gbufBound = false;
	for (int k = 0; k < g_pmUsed; k++)
	{
		viewz = FLOAT2FIXED(zx::hwrender::ComputePlaneMirrorEyeZ(slots[k].z, vz));
		viewpitch = -sp;
		zx::hwrender::ComputePlaneMirrorClip(slots[k].ceiling, slots[k].z, g_clipPlane);
		RenderWorldInto(ctx, g_pm[k]);
	}
	viewz = sz;
	viewpitch = sp;
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	g_clipPlane[0] = g_clipPlane[1] = g_clipPlane[2] = g_clipPlane[3] = 0.f;
	g_gbufBound = savedGBuf;
	for (int k = 0; k < g_pmUsed; k++) HandOverViewTarget(ctx, g_pm[k]);
}

// [rc4l] In the player's view, before the translucent pass: each reflective piece painted from its plane's reflection.
static void DrawPlaneMirrorSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_drawingMainView || g_pmUsed == 0 || !g_pmPSO || !g_vb) return;
	Diligent::IBuffer *vbs[] = { g_vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_pmPSO);
	int bound = -1;
	for (unsigned i = 0; i < g_blendBatches.Size(); i++)
	{
		const SceneBatch &b = g_batches[g_blendBatches[i]];
		if (b.mirrorSlot < 0 || b.mirrorSlot >= g_pmUsed || !g_pm[b.mirrorSlot].srb) continue;
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
static ViewTarget g_stack[zx::levelmesh::kMaxStackViews];
static int g_stackDrawn = 0;   // [rc4l] Views rendered this frame.
static Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_stackShowSRB;   // [rc4l] fua_dg_stackshow's blit

static void ReleaseStackPortals()
{
	for (int k = 0; k < zx::levelmesh::kMaxStackViews; k++) g_stack[k].Release();
	g_stackShowSRB.Release();
	g_stackSurfPSO.Release();
	g_stackDrawn = 0;
	g_stackVisReady = false;
}

// [rc4l] Before the frame and after the sky's picture, which each view paints behind its own world.
static void RenderStackPortals(Diligent::IDeviceContext *ctx)
{
	g_stackDrawn = 0;
	int n = zx::levelmesh::StackViewCount();
	if (n > fua_dg_stackportals) n = fua_dg_stackportals;
	if (n <= 0 || !g_vb || !g_cb) return;
	if (!g_stackSurfPSO)
		g_stackSurfPSO = CreateScreenSurfacePSO("fua stack surface", false, Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
	if (!g_stackSurfPSO) return;

	int visWords = 0;
	const unsigned int *visBits = zx::levelmesh::StackVisibility(visWords);
	g_stackVisReady = visBits != NULL && g_stackVisBuf && (unsigned int)visWords <= g_stackVisCapacity;
	if (g_stackVisReady)
		ctx->UpdateBuffer(g_stackVisBuf, 0, (Diligent::Uint64)visWords * sizeof(unsigned int), visBits,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

	const fixed_t sx = viewx, sy = viewy;
	float savedMVP[16];
	for (int k = 0; k < 16; k++) savedMVP[k] = g_mvp[k];
	const bool savedGBuf = g_gbufBound;
	g_gbufBound = false;
	for (int k = 0; k < n; k++)
	{
		if (!EnsureViewTarget(g_stack[k], g_stackSurfPSO, "uSky", "fua stacked view")) break;
		const zx::levelmesh::StackViewInfo v = zx::levelmesh::StackView(k);
		// [rc4l] No clip plane: GL's stacked walk clips nothing, and the cull already keeps to what it drew.
		viewx = sx + FLOAT2FIXED(v.dx);
		viewy = sy + FLOAT2FIXED(v.dy);
		g_stackPassSlot = k;
		RenderWorldInto(ctx, g_stack[k]);
		g_stackDrawn = k + 1;
	}
	g_stackPassSlot = -1;
	viewx = sx; viewy = sy;
	for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
	g_gbufBound = savedGBuf;
	for (int k = 0; k < g_stackDrawn; k++) HandOverViewTarget(ctx, g_stack[k]);
}

// [rc4l] What the stacked views did last frame, for fua_dg_stackinfo: views drawn, whether the cull had its bits, and how many subsectors each view's walk reached.
void StackInfo(FString &out)
{
	int words = 0;
	const unsigned int *bits = zx::levelmesh::StackVisibility(words);
	int reached[zx::levelmesh::kMaxStackViews] = {};
	for (int w = 0; w < words; w++)
		for (int b = 0; b < 32; b++)
			if (bits[w] & (1u << b)) reached[(w * 32 + b) % zx::levelmesh::kMaxStackViews]++;
	out.Format("stacked views: %d drawn, cull %s, %d words; subsectors and sidedefs drawn by view 0: %d, view 1: %d (of %d)",
		g_stackDrawn, g_stackVisReady ? "on" : "off", words, reached[0], reached[1], numsubsectors + numsides);
}

// [rc4l] In the player's view, with the sky: each stacked plane painted from its view's picture, and depth-written so nothing behind it shows through.
static void DrawStackSurfaces(Diligent::IDeviceContext *ctx)
{
	if (!g_drawingMainView || g_stackDrawn == 0 || !g_stackSurfPSO) return;
	static TArray<SceneVertex> sv;
	unsigned int first[zx::levelmesh::kMaxStackViews], count[zx::levelmesh::kMaxStackViews];
	sv.Clear();
	for (int k = 0; k < g_stackDrawn; k++)
	{
		int n = 0;
		const FFlatVertex *src = zx::levelmesh::StackViewVertices(k, n);
		first[k] = sv.Size();
		for (int i = 0; i < n; i++) sv.Push(ToSceneVertex(src[i], 0));
		// [rc4l] And the cap GL closes the stack's stencil with, so every unblocked ray past the plane sees the view, not only those through it.
		const bool ceiling = zx::levelmesh::StackView(k).ceiling;
		for (int c = 0; c < 6; c++)
		{
			float x, y, z;
			zx::levelmesh::ComputeStackCapCorner(ceiling, c, x, y, z);
			FFlatVertex fv;
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
static void ShowStackView(Diligent::IDeviceContext *ctx)
{
	if (!fua_dg_stackshow || g_stackDrawn == 0 || !g_skyPortalPSO || !g_stack[0].color) return;
	if (!g_stackShowSRB) g_skyPortalPSO->CreateShaderResourceBinding(&g_stackShowSRB, true);
	if (!g_stackShowSRB) return;
	if (auto *v = g_stackShowSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uTex"))
		v->Set(g_stack[0].color->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	ctx->SetPipelineState(g_skyPortalPSO);
	ctx->CommitShaderResources(g_stackShowSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs dr;
	dr.NumVertices = 3;
	ctx->Draw(dr);
}

static void RenderMirrors(Diligent::IDeviceContext *ctx)
{
	// [rc4l] A switch, so the pass can be BISECTED against the TDR.
	//
	// A planar mirror re-renders the world from the mirror's viewpoint, once per mirror. These maps
	// carry six, against fifty thousand pieces, and the crash under investigation is a GPU operation
	// running past the two-second timeout at a level transition -- so this pass is a suspect and
	// there was no way to take it out of the picture. Every other suspect has been ruled out by
	// switching it off and measuring; this one could not be, which is reason enough for the cvar.
	if (!fua_dg_mirrors_on) return;
	// [rc4l] GL draws a mirror's wall instead of its reflection with gl_mirrors off, and the bake follows it (ComputeIsMirrorWall).
	if (!gl_mirrors) return;
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

		// [rc4l] A mirror you are BEHIND cannot be seen, so do not render the world into it.
		//
		// This loop re-rendered the entire scene once per mirror, unconditionally. These maps carry
		// six, so a frame was seven full scene renders over fifty thousand pieces -- and at a level
		// transition, with every material resolving for the first time and the batch layout not yet
		// settled, that is enough GPU work in one submit to cross the two-second timeout. Windows
		// resets the device and the driver terminates the process: the TDR that reads as a silent
		// crash after ~20 map loads. Measured, same build, same session, four sequences each:
		// mirrors on, 2 failures and 2 TDRs; mirrors off, 0 and 0.
		//
		// `dist` is which side of the mirror plane the camera is on, and it was already being
		// computed and then ignored. Negative means the surface faces away -- there is nothing to
		// see and nothing to draw, so the reflection is pure waste. This is exact, not an
		// approximation: it removes renders that could not have contributed a pixel.
		if (dist <= 0.f) continue;
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
		g_lineMirror = m.line;
		DrawWorld(ctx);
		g_lineMirror = -1;

		// Back to the screen, with the real camera, and draw this mirror's surface.
		g_clipPlane[0] = g_clipPlane[1] = g_clipPlane[2] = g_clipPlane[3] = 0.f;
		for (int k = 0; k < 16; k++) g_mvp[k] = savedMVP[k];
		ctx->SetRenderTargets(1, &brtv, bdsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		{
			Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
			WriteSceneConstants(&cb[0], nullptr, (float)g_mirrorW, (float)g_mirrorH, 0.f);
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
	if (!ComputeSceneTargetsRebuild(g_sceneDepth.RawPtr() != nullptr, g_sceneDepthW, g_sceneDepthH, (int)sd.Width, (int)sd.Height))
		return g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	if (fua_dg_verbose)
		Printf("Diligent scene targets rebuilt at %ux%u (were %dx%d)\n", sd.Width, sd.Height, g_sceneDepthW, g_sceneDepthH);

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

	// The sky portal is a whole second view of the map, so it is rendered here, beside the frame
	// rather than inside it -- DrawWorld cannot be re-entered while it is deriving this frame.
	RenderSkyPortal(ctx);
	RenderStackPortals(ctx);
	RenderPlaneMirrors(ctx);

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

	g_drawingMainView = true;
	DrawWorld(ctx);
	g_drawingMainView = false;
	RenderMirrors(ctx);
	ShowStackView(ctx);

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
		RecordPresentInterval();
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

	if (!CreateViewTextures(w, h, "fua camera", c->color, c->depth)) { delete c; return NULL; }
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

	g_inCameraPass = true;
	DrawWorld(ctx);
	g_inCameraPass = false;

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

// [rc4l] Both stages, because pixel shaders read uCameraPos for radial fog and a declared block left unbound reads garbage rather than failing.
bool CreateStructuredBuffer(const char *name, unsigned stride, unsigned long long count, const void *data,
	Diligent::RefCntAutoPtr<Diligent::IBuffer> &out)
{
	Diligent::BufferDesc bd;
	bd.Name = name;
	bd.Size = (Diligent::Uint64)count * stride;
	bd.Usage = Diligent::USAGE_DEFAULT;
	bd.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	bd.Mode = Diligent::BUFFER_MODE_STRUCTURED;
	bd.ElementByteStride = stride;
	Diligent::BufferData init;
	init.pData = data;
	init.DataSize = bd.Size;
	out.Release();
	GetDevice()->CreateBuffer(bd, data != NULL ? &init : nullptr, &out);
	return out != nullptr;
}

bool BindSceneConstants(Diligent::IPipelineState *pso)
{
	auto *vv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Constants");
	if (vv) vv->Set(g_cb);
	if (auto *pv = pso->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Constants")) pv->Set(g_cb);
	return vv != NULL;
}
const float *SceneMVP() { return g_mvp; }

// [rc4l] What the backend spends its frame on, phase by phase.
//
// The backend adds ~5 ms to a Sunder MAP16 frame while its own drawing measures 0.25 ms of submit
// and 0.15 of GPU. Everything between those two numbers is bookkeeping, and guessing which part has
// already cost one wrong answer today: the geometry rebuild was the obvious suspect, was genuinely
// running 6506 times a frame-set, was fixed to 22, and the frame did not move. So it gets measured.
struct PhaseClock
{
	cycle_t geometry, lights, clusters, constants, animation, draw;
	void Reset() { geometry.Reset(); lights.Reset(); clusters.Reset(); constants.Reset();
		animation.Reset(); draw.Reset(); }
};
static PhaseClock g_phase;

void PhaseReport(FString &out)
{
	out.Format("backend phases (last frame, ms): geometry %.3f, lights %.3f, clusters %.3f, "
		"constants %.3f, animation %.3f, draw %.3f",
		g_phase.geometry.TimeMS(), g_phase.lights.TimeMS(), g_phase.clusters.TimeMS(),
		g_phase.constants.TimeMS(), g_phase.animation.TimeMS(), g_phase.draw.TimeMS());
}

static void DrawWorld(Diligent::IDeviceContext *ctx)
{
	// [rc4l] Lights are collected BEFORE the constants are written -- the shader reads the count from
	// there, so collecting afterwards would light every frame with the previous frame's count.
	g_phase.Reset();
	g_batchesDrawn = g_worldDraws = 0;
	g_phase.geometry.Clock();  RefreshMovedGeometry(ctx);   g_phase.geometry.Unclock();
	g_phase.lights.Clock();    CollectDynamicLights(ctx);   g_phase.lights.Unclock();
	g_phase.clusters.Clock();  BuildLightClusters(ctx);     g_phase.clusters.Unclock();
	// [rc4l] Before anything is recorded: rebuilding a binding that a command buffer already holds is
	// a use-after-free, and this only does work on the frames the material table actually changed.
	if (fua_dg_bindless) RefreshBindless();
	g_phase.constants.Clock();

	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		WriteSceneConstants(&cb[0], g_clipPlane, (float)GetSwapChain()->GetDesc().Width,
			(float)GetSwapChain()->GetDesc().Height, BindlessActive() ? (float)MaterialSlotCount() : 0.f);
	}

	g_phase.constants.Unclock();

	g_phase.animation.Clock();
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
			if (auto *srb = GetMaterialSRB(BatchPSO(b.blend), now)) b.srb = srb;
		}
	}
	g_phase.animation.Unclock();

	g_phase.draw.Clock();

	// [rc4l] Sky first, with depth off, so the world paints over whatever it does not cover.
	if (fua_dg_sky)
	{
		// mSky1Pos is in degrees and advances with gl_frameMS, so it must be read every frame.
		const float scroll = (GLRenderer != NULL) ? GLRenderer->mSky1Pos : 0.f;
		g_skyAngle = (-180.0f + scroll) * 3.14159265f / 180.0f;
		// [rc4l] A sky that is a PLACE takes precedence over any texture.
		//
		// When the map has an ASkyViewpoint, the sky is the world seen from it and there is no sky
		// texture to speak of -- maps that use one leave sky1texture as the null placeholder, which
		// is why this used to draw a blue-and-white checkerboard here.
		// [rc4l] Painted full-screen first, as GL's RenderFirstSkyPortal does, so sky no captured wall covers is still sky; the surfaces drawn later only cut what lies beyond it.
		if (!BlitSkyPortal(ctx))
		{
			EnsureSky();
			DrawSky(ctx);
			DrawSkyOccluders(ctx);
		}
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
	// [rc4l] With nothing bound per batch, a batch is only a range -- so adjacent ones are ONE draw.
	//
	// A batch exists to share a material binding. That was its whole reason for being, and with the
	// array carrying the materials it has none: the opaque pass writes depth and tests it, so the
	// order within it does not matter, and 165 ranges that happen to be contiguous in the vertex
	// buffer are 165 draw calls describing one interval.
	//
	// The run is flushed whenever the chain breaks -- a translucent batch in the middle, an empty
	// one, a culled one -- so this is exactly the old loop with the boundaries removed, and it stays
	// correct when batch culling is on.
	for (int rep = 0; rep < g_drawRepeat; rep++)
	{
		Diligent::IShaderResourceBinding *bound = NULL;
		unsigned runFirst = 0, runCount = 0;
		for (unsigned bi = 0; bi <= g_batches.Size(); bi++)
		{
			SceneBatch *b = (bi < g_batches.Size()) ? &g_batches[bi] : NULL;
			bool take = b != NULL && b->count != 0 && b->blend == 0;
			// [rc4l] The portal draws only what stands NEAR ITS OWN CAMERA.
			//
			// A sky portal renders from a viewpoint inside a sealed room off the side of the map.
			// GL walks the BSP from there and reaches only that room; the map-derived bake has no
			// walk, so this pass would otherwise draw the whole level from inside the room -- the
			// map's own walls all around the camera, leaving the sky a rectangle in the middle of
			// the window. Measured: the sky band at the frame edges read 1.96 and 2.25 against GL's
			// 18.57 and 8.48, and matched GL exactly with the derived bake switched off.
			//
			// A reach test rather than a sector flood, because a batch already carries its bounds
			// and a skybox room is small and far from everything else. See docs/skybleed-scope.md.
			if (take && g_inSkyPortal && fua_dg_skyportal_reach > 0.f &&
			    ComputeBeyondSkyPortalReach(b->minX, b->maxX, b->minZ, b->maxZ,
			                                g_skyPortalCamX, g_skyPortalCamY, fua_dg_skyportal_reach))
				take = false;

			Diligent::IShaderResourceBinding *srb = NULL;
			if (take)
			{
				// [rc4l] One binding for the whole pass when the array is carrying the materials. This
				// is the cost the culling experiment could not remove: 166 batches were 0.445 ms of
				// submit on Sunder MAP16, almost all of it committing a descriptor set per batch.
				srb = WorldSRB(g_gbufBound ? g_maskedGBufPSO.RawPtr() : g_maskedPSO.RawPtr());
				if (srb == NULL)
				{
					// Re-resolve rather than skip: a binding dropped by a resize would otherwise leave
					// that material missing from the world until the next upload.
					srb = BatchSRB(*b);
				}
				if (srb == NULL) take = false;
			}

			// A run continues only while the next range starts where the last one ended AND wants the
			// same binding. Without bindless that is one batch at a time, exactly as before.
			const bool extends = fua_dg_mergedraws && take && runCount != 0 && srb == bound &&
				b->first == runFirst + runCount;
			if (extends) { runCount += b->count; g_batchesDrawn++; continue; }

			if (runCount != 0)
			{
				Diligent::DrawAttribs draw;
				draw.NumVertices = runCount;
				draw.StartVertexLocation = runFirst;
				draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
				ctx->Draw(draw);
				g_worldDraws++;
				runCount = 0;
			}
			if (!take) { bound = NULL; continue; }
			if (srb != bound)
			{
				ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				bound = srb;
			}
			runFirst = b->first; runCount = b->count;
			g_batchesDrawn++;
		}
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
	// [rc4l] The SAME question the spawn side asks, which it was not.
	//
	// A mark is made when ProjectedDecalsActive() says so -- fua_decalmode for walls OR
	// fua_decal_flats for floors and ceilings -- and was drawn only when fua_decalmode was set. So
	// with the default decalmode a floor mark was built, stored, aged and faded, and never once
	// drawn: fua_projdecals_stats reported ten live decals against a pass that ran zero boxes in
	// zero draws. fua_decal_flats could not produce a visible mark on its own, which is the whole
	// of what that switch is for.
	//
	// Two gates on one feature will drift; there is now one, and both sides call it.
	if (zx::levelmesh::ProjectedDecalsActive()) DrawDeferredDecals(ctx);

	// [rc4l] Sprites are built here but ALL of them draw in the sorted pass below, never in an
	// opaque one. See DrawBlended.
	BuildDynamic(ctx);

	// [rc4l] ...except the solid ones, which belong with the world: a model has to occlude itself.
	DrawSolidDyn(ctx);

	// [rc4l] Horizon lines, painted with the planes they open onto -- see features/horizon; built for the player's eye, so a stacked view takes none.
	if (g_stackPassSlot < 0) DrawHorizonPlanes(ctx);

	// [rc4l] ...and the sky as geometry, before the blend pass because it is opaque.
	DrawSkySurfaces(ctx);
	DrawStackSurfaces(ctx);


	DrawPlaneMirrorSurfaces(ctx);

	// [rc4l] Then everything that blends -- translucent 3D floors, translucent middle textures and
	// translucent sprites -- in ONE back-to-front pass, so the world and the sprites sort against each
	// other rather than being layered by which pass came last.
	DrawBlended(ctx);
	g_phase.draw.Unclock();
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
		// [rc4l] The material SLOT TABLE holds the same stale pointers, and it is worse than the caches
		// because something reads it every frame.
		//
		// Between a level change and the new scene upload, RefreshBindless would fill the array from
		// the old level's FMaterial pointers -- and GetMaterialSRV caches what it makes of them, keyed
		// on those same addresses. The new level then allocates its materials at the addresses the old
		// ones just vacated, hits the poisoned cache, and every surface comes up wearing some other
		// surface's texture. 93% of the frame on dbab04, and only ever after a map change, which is why
		// it looked like the map rather than the transition.
		ResetMaterialSlots();
		ReleaseWorldSRBs();
		// [rc4l] And the PIPELINES, because the material array lives on them.
		//
		// uMaterials is a STATIC variable, which means Diligent stores it on the pipeline and copies it
		// into every binding made from that pipeline. A level change refills it -- and on a pipeline
		// that has already handed out bindings, refilling does not take: the array keeps the PREVIOUS
		// level's textures, so the new level renders with its geometry perfect and every surface
		// wearing the wrong picture. A map loaded directly is fine because the array is filled before
		// any binding exists, which is why this only ever showed up after a map change.
		ReleaseScenePipelines();
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
		// [rc4l] ...and the walls straight from the map, without waiting for the traversal.
		//
		// ArmFullBake pushes GL's own walk along until every subsector has been visited, which is how
		// the mesh has always been filled. A seg either has wall parts or it does not, and the sidedef
		// says which -- so this asks the map directly and the traversal is left to fill in the kinds
		// the derivation does not do yet.
		if (fua_surface_mapbake_auto)
		{
			const int built = zx::levelmesh::BakeLevelFromMap();
			// ...and the FLATS, which are the other half of not needing the traversal. GLFlats come
			// from the same walk, so without this the level renders every wall over a bare sky.
			const int flats = zx::levelmesh::BakeFlatsFromMap();
			if (built > 0 || flats > 0)
				Printf("vulkan: %d wall parts and %d planes built from the map\n", built, flats);
		}
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

// [rc4l] Is the standalone path actually carrying the frame?
//
// Three things must be true and the cvar is one of them: the backend has to be up, the level has to
// have been baked (a bake runs inside GL's BSP walk, so the first frames after switching are still
// GL frames), and there has to be geometry. Anything else and GL keeps the frame -- falling back
// silently is right here, because the alternative is a black screen.
// [rc4l] See hud2d.h. Deliberately does NOT ask about fua_dg_standalone: with the embedded window
// up, GL's output is discarded either way.
// [rc4l] THE FRAME, which nothing else in this backend measures.
//
// Every clock in this file and in stat rendertimes is a COMPONENT: All= is GL's render, the backend
// phases are the passes, fua_diligent_bench re-renders the resident mesh in a loop without running a
// game frame at all. Each of them can read 0.7 ms while the player is looking at 146 fps, because
// none of them measures the interval between one picture appearing and the next -- which is the only
// number a player can feel, and the only one that shows a stutter.
//
// So: wall clock between successive presents, kept as a ring, reported as percentiles by
// fua_frametimes. A stutter is a p99 far above the median, and no average will ever show it.
namespace {
const int kFrameRing = 1024;
double  g_frameMs[kFrameRing];
int     g_frameCount = 0;
int     g_frameHead  = 0;
bool    g_frameHavePrev = false;
std::chrono::steady_clock::time_point g_framePrev;
}

// [rc4l] What each recorded frame was made of, in step with g_frameMs.
//
// A percentile on its own says a frame was slow and nothing about why, and every attribution in this
// work that was reasoned rather than measured turned out wrong. These ride the same ring as the
// interval so the slow frames can be asked directly what they were carrying.
struct FrameParts { float invalMs; int sectors; int repaints; int runs; int patchVerts; bool ticked; };
static FrameParts g_frameParts[kFrameRing];
static int g_lastLevelTime = -1;

void RecordPresentInterval()
{
	const auto now = std::chrono::steady_clock::now();
	if (g_frameHavePrev)
	{
		const double ms = std::chrono::duration<double, std::milli>(now - g_framePrev).count();
		g_frameMs[g_frameHead] = ms;
		FrameParts &fp = g_frameParts[g_frameHead];
		fp.invalMs    = (float)zx::levelmesh::InvalidationMsLastFrame();
		fp.sectors    = zx::levelmesh::SectorsDirtyLastFrame();
		fp.repaints   = g_fRepaints;
		fp.runs       = g_fRuns;
		fp.patchVerts = g_fPatchVerts;
		fp.ticked     = (level.time != g_lastLevelTime);
		g_lastLevelTime = level.time;
		g_fRepaints = g_fRuns = g_fPatchVerts = g_fPatches = 0;
		g_frameHead = (g_frameHead + 1) % kFrameRing;
		if (g_frameCount < kFrameRing) g_frameCount++;
	}
	g_framePrev = now;
	g_frameHavePrev = true;
}

void ResetFrameTimes() { g_frameCount = 0; g_frameHead = 0; g_frameHavePrev = false; }

CCMD( fua_frametimes )
{
	if (g_frameCount < 8) { Printf( "fua_frametimes: not enough frames yet -- play for a second.\n" ); return; }
	TArray<double> v;
	v.Resize( (unsigned)g_frameCount );
	for (int i = 0; i < g_frameCount; i++) v[i] = g_frameMs[i];
	std::sort( &v[0], &v[0] + g_frameCount );
	const double med = v[g_frameCount / 2];
	const double p95 = v[(int)(g_frameCount * 0.95)];
	const double p99 = v[(int)(g_frameCount * 0.99)];
	double sum = 0.0; for (int i = 0; i < g_frameCount; i++) sum += v[i];
	const double mean = sum / g_frameCount;
	// How many frames took more than twice the median -- that is what a stutter IS.
	int hitches = 0; for (int i = 0; i < g_frameCount; i++) if (v[i] > med * 2.0) hitches++;
	Printf( "fua_frametimes over %d frames: min %.2f  med %.2f  mean %.2f  p95 %.2f  p99 %.2f  max %.2f ms\n",
		g_frameCount, v[0], med, mean, p95, p99, v[g_frameCount - 1] );
	Printf( "  %.0f fps at the median, %.0f at p99   |   %d frames (%.1f%%) took over twice the median\n",
		1000.0 / med, 1000.0 / p99, hitches, 100.0 * hitches / g_frameCount );
	// [rc4l] WHAT the slow frames were carrying, next to what a median frame carries.
	//
	// A percentile says a frame was slow and nothing about why. Every attribution in this work that
	// was reasoned instead of measured turned out to be wrong -- the stutter was blamed on moving
	// geometry when three quarters of it was flickering lights. So the components ride the same ring
	// as the interval, and the slow frames are asked directly.
	{
		double slowMs = 0, medMs = 0;
		double sInval = 0, mInval = 0;
		double sRep = 0, mRep = 0, sRuns = 0, mRuns = 0, sPatch = 0, mPatch = 0, sSect = 0, mSect = 0;
		int nSlow = 0, nMed = 0, slowTicked = 0, medTicked = 0;
		for (int i = 0; i < g_frameCount; i++)
		{
			const FrameParts &fp = g_frameParts[i];
			const bool slow = g_frameMs[i] >= p99;
			double &ms = slow ? slowMs : medMs; ms += g_frameMs[i];
			(slow ? sInval : mInval) += fp.invalMs;
			(slow ? sRep   : mRep)   += fp.repaints;
			(slow ? sRuns  : mRuns)  += fp.runs;
			(slow ? sPatch : mPatch) += fp.patchVerts;
			(slow ? sSect  : mSect)  += fp.sectors;
			if (fp.ticked) { if (slow) slowTicked++; else medTicked++; }
			if (slow) nSlow++; else nMed++;
		}
		// [rc4l] SAY SO when the sim was frozen, because the numbers stop meaning frame rate.
		//
		// A paused engine presents once per tic, so every reading collapses to ~28.6 ms / 35 fps
		// however fast the renderer actually is. rc4l reported "sub 300" at a vantage that measures
		// 305 fps running and 37 fps paused -- the menu was open. Reading a frame time off a frozen
		// sim has now misled this work twice, so the report refuses to be quoted out of context.
		if (slowTicked + medTicked == 0 && g_frameCount > 32)
			Printf( "  NOTE: no game tic ran during this sample -- the sim was paused or the window "
			        "was inactive.\n        A frozen sim presents once per tic, so this is the 35 Hz tic "
			        "rate, not the renderer's speed.\n" );
		if (nSlow > 0 && nMed > 0)
		{
			Printf( "  what a frame carries      %-12s %-12s\n", "p99+", "the rest" );
			Printf( "    frames                  %-12d %-12d\n", nSlow, nMed );
			Printf( "    mean frame time         %-12.2f %-12.2f ms\n", slowMs/nSlow, medMs/nMed );
			Printf( "    ran a game tic          %-11.0f%% %-11.0f%%\n",
				100.0*slowTicked/nSlow, 100.0*medTicked/nMed );
			Printf( "    sectors invalidated     %-12.1f %-12.1f\n", sSect/nSlow, mSect/nMed );
			Printf( "    invalidation loop       %-12.3f %-12.3f ms\n", sInval/nSlow, mInval/nMed );
			Printf( "    surfaces repainted      %-12.1f %-12.1f\n", sRep/nSlow, mRep/nMed );
			Printf( "    repaint uploads         %-12.1f %-12.1f\n", sRuns/nSlow, mRuns/nMed );
			Printf( "    vertices patched        %-12.1f %-12.1f\n", sPatch/nSlow, mPatch/nMed );
			Printf( "    unattributed            %-12.3f %-12.3f ms  (tic, draw, present)\n",
				slowMs/nSlow - sInval/nSlow, medMs/nMed - mInval/nMed );
		}
	}

	// [rc4l] The SEQUENCE, not just the percentiles. A stutter has a shape: every-Nth-frame is a
	// timer, bursts are a stall, and alternating is a double-buffer fighting something. Percentiles
	// cannot tell those apart and the raw run can.
	if (argv.argc() > 1)
	{
		FString line;
		const int n = (g_frameCount < 48) ? g_frameCount : 48;
		const int start = (g_frameHead - n + kFrameRing) % kFrameRing;
		for (int i = 0; i < n; i++)
		{
			line.AppendFormat( "%.1f ", g_frameMs[(start + i) % kFrameRing] );
			if ((i % 12) == 11) { Printf( "  %s\n", line.GetChars() ); line = ""; }
		}
		if (line.Len()) Printf( "  %s\n", line.GetChars() );
	}
	ResetFrameTimes();
}

bool BackendPresenting()
{
	if (!fua_vulkan || !fua_dg_embed) return false;
	return GetSwapChain() != NULL;
}

bool StandaloneActive()
{
	if (!fua_dg_standalone || !fua_vulkan) return false;
	if (!g_standaloneBaked || !g_vb || g_sceneVerts == 0) return false;
	return true;
}

void LiveFrame()
{
	// [rc4l] fua_vulkan is the user-facing switch and sets itself up; fua_diligent_live is the manual
	// override that assumes someone already ran the bake by hand.
	const bool autoReady = fua_vulkan ? AutoSetupForLevel() : false;

	// [rc4l] Bake the level the moment standalone is asked for, because from then on nothing else
	// will: the bake runs inside GL's BSP walk, and that walk is what this mode stops. Armed rather
	// than run here, and only once per switch-on.
	if (fua_dg_standalone && autoReady && !g_standaloneBaked)
	{
		if (g_standaloneArmed == 0)
		{
			zx::levelmesh::ArmFullBake();
			Printf("standalone: baking the level once, then GL stops deriving the scene.\n");
			Printf("standalone: sprites and moving geometry are NOT fed in this mode yet.\n");
		}
		if (++g_standaloneArmed > 3) g_standaloneBaked = true;   // a frame to bake, one to upload
	}
	if (!fua_dg_standalone) { g_standaloneBaked = false; g_standaloneArmed = 0; }

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
void SlotReport(int n, FString &out)
{
	out.Format("slots %u, batches %u" "\n", g_matSlotTable.Size(), g_batches.Size());
	for (int i = 0; i < n && (unsigned)i < g_batches.Size(); i++)
	{
		const SceneBatch &b = g_batches[i];
		int slot = -1;
		for (unsigned k = 0; k < g_matSlotTable.Size(); k++)
			if (g_matSlotTable[k].material == b.material && g_matSlotTable[k].translation == 0) { slot = (int)k; break; }
		const char *bname = "?";
		if (b.material && ((FMaterial *)b.material)->tex) bname = ((FMaterial *)b.material)->tex->Name.GetChars();
		const char *sname = "-";
		if (slot > 0 && g_matSlotTable[slot].material &&
		    ((FMaterial *)g_matSlotTable[slot].material)->tex)
			sname = ((FMaterial *)g_matSlotTable[slot].material)->tex->Name.GetChars();
		// [rc4l] ...and what the VERTEX actually says, which is the only number the GPU sees.
		// A lookup that agrees with the table proves nothing if the buffer was written against a
		// different table.
		const unsigned int pidx = (b.first < g_sceneVB.Size()) ?
			(unsigned int)(g_sceneVB[b.first].pieceIndex + 0.5f) : 0xffffffffu;
		const int inVB = (pidx < g_scenePieceData.Size()) ?
			(int)(g_scenePieceData[pidx].matSlot + 0.5f) : -1;
		// The view itself, because two slots resolving to ONE view is the signature of a cache keyed
		// on an address that two different materials have held.
		const void *srv = (slot > 0) ? (const void *)GetMaterialSRV(g_matSlotTable[slot].material,
			g_matSlotTable[slot].translation) : NULL;
		const char *vname = "-";
		if (inVB > 0 && (unsigned)inVB < g_matSlotTable.Size() && g_matSlotTable[inVB].material &&
		    ((FMaterial *)g_matSlotTable[inVB].material)->tex)
			vname = ((FMaterial *)g_matSlotTable[inVB].material)->tex->Name.GetChars();
		out.AppendFormat("  batch %d verts %u..%u material %s -> slot %d holding %s (view %p) | vertex says %d = %s" "\n",
			i, b.first, b.first + b.count, bname, slot, sname, srv, inVB, vname);
	}
}

void DynStats(FString &report)
{
	int sprites = zx::levelmesh::SpritePieceCount();
	report.Format("2D: %d quads -> %d draws | dynamic: %d sprites -> %d draws (%d submitted), %d tris "
		"[opaque %d, translucent %d, additive %d, fuzz %d]",
		g_quads2D, g_draws2D, sprites, g_dynDraws, g_blendSubmits, g_dynTris,
		g_dynByBlend[0], g_dynByBlend[1], g_dynByBlend[2], g_dynByBlend[3]);
	FString anim;
	anim.Format(" | %d animation frame swaps since load | light buffer %d vec4s (~%d lights)",
		g_animSwaps, g_lightCount * 2, g_lightCount);
	FString geo;
	geo.Format(" | geometry: %d scene rebuilds since load, last dirty %u..%u",
		g_geomRebuilds, g_lastDirtyLo, g_lastDirtyHi);
	FString cull;
	cull.Format(" | batches: %d drawn in %d draw calls | bindless %s: %d slots, %d white",
		g_batchesDrawn, g_worldDraws,
		BindlessActive() ? "on" : "off", MaterialSlotCount(), g_matSlotsWhite);
	cull.AppendFormat(", dyn slots: %d seen, max %d, %d refused, %d with no material",
		g_dynSlotSeen, g_dynSlotMax, g_dynSlotRefused, g_dynSlotNoMaterial);
	cull.AppendFormat(", dyn draws: %d bindless, %d per-material, pipelines [%s]",
		g_dynBindless, g_dynPerMaterial, g_fillState);
	cull.AppendFormat(", sky surfaces: %d draws since load", g_skyDraws);
	cull.AppendFormat(", viewer sprites for mirrors: %d", g_dynViewerRuns);
	cull.AppendFormat(", dyn build %.3f ms", g_dynBuildMs);
	{
		// [rc4l] How much of the world the DERIVATION built, rather than transcribed from GL. The
		// number is the point of features/surfaces: it goes up as categories move across.
		int derived = 0, fellBack = 0;
		zx::surfaces::GetDeriveStats(derived, fellBack);
		int mid = 0, special = 0, notex = 0, nospan = 0, seam = 0;
		zx::surfaces::GetDeriveFallbacks(mid, special, notex, nospan, seam);
		cull.AppendFormat(" | derived surfaces: %d built, %d from the capture"
			" (%d two-sided middles, %d special walls, %d no texture, %d no span, %d seam-split)",
			derived, fellBack, mid, special, notex, nospan, seam);
	}
	cull.AppendFormat(" | array built %d times, %d remap uploads, slots 1..7 got:", g_fillCount, g_remapUploads);
	for (int fi = 1; fi < 8; fi++)
		cull.AppendFormat(" %d=%s(%p)", fi, g_fillNames[fi].IsNotEmpty() ? g_fillNames[fi].GetChars() : "-",
			g_fillViews[fi]);
	report += cull;
	FString phases;
	PhaseReport(phases);
	report += " | ";
	report += phases;
	int added = 0, resized = 0, rebatched = 0;
	zx::levelmesh::MeshLayoutReasons(added, resized, rebatched);
	FString why;
	why.Format(" | layout gen %u (built %u): %d added, %d resized, %d rebatched, %d repaints applied in %d uploads",
		zx::levelmesh::MeshLayoutGeneration(), g_builtLayoutGen, added, resized, rebatched,
		g_geomRepaints, g_repaintRuns);
	report += why;
	FString patches;
	patches.Format(" | %d geometry patches (%d verts moved), %d appends (%d new, %d resized, %d revived) (%d verts, %d%% of slack)",
		g_geomPatches, g_geomPatchVerts, g_geomAppends, g_appendNoSlot, g_appendResized, g_slotRevived, g_appendedVerts,
		g_vbCapacity ? (int)(100.0 * g_appendedVerts / (g_vbCapacity / 8.0)) : 0);
	report += patches;
	report += geo;
	// [rc4l] What the grid actually did this frame. "0 cells" with lights alive means the binning
	// pass bailed -- a screen bigger than the table, or the cvar off -- and the shader silently fell
	// back to testing every light, which is correct but is not the thing being measured.
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
namespace zx { namespace hwrender { void StackInfo(FString &out); }}
CCMD( fua_dg_stackinfo )
{
	FString report;
	zx::hwrender::StackInfo( report );
	Printf( "%s\n", report.GetChars( ) );
}
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

// [rc4l] What a static shader array would actually cost, before adding one.
//
// A bindless material array is declared STATIC, and Diligent copies a pipeline's static resources
// into EVERY shader resource binding made from it. This backend makes one SRB per (pipeline,
// material, translation) -- see g_matSRBs -- so an N-slot array does not cost N descriptors, it costs
// N times the number of SRBs. That is the whole of the unexplained 64-works/128-dies ceiling the
// first bindless attempt hit: not a pool size, not a device limit, but bindless and per-material
// SRBs being mutually exclusive by construction.
//
// This prints the multiplier for the map that is loaded, so the next attempt can see the number
// before it spends an afternoon on the symptom.
// [rc4l] What each batch thinks its material is, against what the slot table says.
//
// A surface drawn with another surface's texture has exactly two causes and they need opposite
// fixes: the vertex is carrying the wrong slot number, or the slot is carrying the wrong texture.
// This prints both sides for the first batches so the two can be told apart instead of argued about.
// [rc4l] Do to the texture cache what a level change does, on demand.
//
// The cache is keyed on raw FMaterial*, and a new level puts its materials at the addresses the old
// level's just vacated -- so a cache that outlives the change serves the previous map's picture for
// a pointer that merely happens to match. That is the standing suspect for bindless coming up wrong
// after a map change and right on a fresh load, and this is how to ask it directly: if flushing here
// repairs the frame, the fault is WHEN the cache was filled, not what the array or the vertices say.
CCMD( fua_dg_flushtextures )
{
	zx::hwrender::FlushTextureCache( );
	Printf( "texture cache and every binding dropped; they rebuild next frame\n" );
}

CCMD( fua_dg_bindless_rebuild )
{
	zx::hwrender::DirtyMaterialSlots( );
	Printf( "material array will be rebuilt next frame\n" );
}

CCMD( fua_dg_slots )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 12;
	FString report;
	zx::hwrender::SlotReport( n, report );
	Printf( "%s" "\n", report.GetChars( ) );
}

CCMD( fua_dg_srbcost )
{
	const int slots = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 512;
	int cached = 0, batch = 0;
	zx::hwrender::MaterialSRBCounts( cached, batch );
	const int srbs = cached + batch;
	Printf( "material SRBs live: %d cached + %d batch = %d" "\n",
		cached, batch, srbs );
	Printf( "a %d-slot STATIC sampler array would add %lld combined-image-sampler descriptors" "\n",
		slots, (long long)slots * (long long)srbs );
	Printf( "one SRB per pipeline instead -- which is what bindless is FOR -- would add %d" "\n",
		slots * 13 );
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

namespace zx { namespace hwrender { void LiveFrame() {} void SetMainViewLens(float, float) {} }}

#endif // FUA_DILIGENT
