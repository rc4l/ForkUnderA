// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the Vulkan backend's own files share and nothing outside them includes: their headers, the pass record, the scene's records, and each file's calls.

#ifndef ZX_DGSCENE_INTERNAL_H
#define ZX_DGSCENE_INTERNAL_H

#include "features/hwrender/computation/tracedsprite_compute.h"   // [rc4l] TracedSprite
#include "features/hwrender/computation/texflags_compute.h"   // [rc4l] ComputePieceTexFlags, the bits the scene shaders test
#include "c_dispatch.h"
#include "c_console.h"


#include "EngineFactory.h"
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
#include "features/hwrender/diligent/dgplatform.h"   // [rc4l] the backend window on this platform
#include "features/hwrender/computation/stereo_compute.h"   // [rc4l] StereoMode
#include "features/hwrender/computation/backendview_compute.h"   // [rc4l] BackendCamera
#include "features/surfaces/surfacebuild.h"
#include "features/levelmesh/wallcache.h"   // LevelGeneration, for the per-level auto setup
#include "d_main.h"                          // gamestate
#include "features/levelmesh/flatmesh.h"
#include "features/horizon/horizon.h"   // [rc4l] the horizon lines' sky halves
#include "features/hwrender/computation/skyportal_compute.h"   // [rc4l] what the portal pass may draw
#include "features/levelmesh/computation/packedcolor_compute.h"
#include "features/levelmesh/computation/flatmesh_compute.h"   // [rc4l] kSkyCapture*, the sky list a pass draws
#include "features/hwrender/computation/warp_compute.h"   // [rc4l] the warp texture ripple
#include "features/hwrender/computation/sceneconstants_compute.h"   // [rc4l] the constant block, packed in one place
#include "features/hwrender/computation/scenebinding_compute.h"
#include "features/hwrender/computation/worldpipelines_compute.h"   // [rc4l] the world pipelines, described once
#include "features/hwrender/computation/materialslots_compute.h"    // [rc4l] the material array's size and the shader defines that carry it
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
#include "features/hwrender/computation/batch_compute.h"   // [rc4l] ComputeStaysInBatch, for patching animated surfaces
#include "features/hwrender/computation/passname_compute.h"   // [rc4l] ComputeWorldPassName, for the per-pass timings
#include "features/hwrender/computation/mirrorsheen_compute.h"   // [rc4l] GL's sheen over a line mirror
#include "features/hwrender/computation/desaturate_compute.h"   // [rc4l] a sector's desaturation, as main.fp applies it
#include "features/levelmesh/computation/wallbatch_compute.h"   // [rc4l] ComputeFanTriangleVertex, a mirror quad split as GL fans it
#include "features/hwrender/hud2d.h"
#include "features/horizon/computation/horizon_compute.h"   // [rc4l] ComputeQuadMayBeVisible for mirrors out of sight, ComputeEyeFromViewProjection for the horizon sky
#include "features/hwrender/diligent/dglight_glsl.h"   // [rc4l] the sector light, shared with the horizon pass
#include "v_video.h"
#include "features/rendersettings/rendersettings.h"   // [rc4l] the render settings it reads
#include "features/hwrender/computation/lightcluster_compute.h"
#include "features/dynlights/dynlight.h"   // [rc4l] ADynamicLight, FLightNode
#include "p_local.h"
#include "doomstat.h"
#include "templates.h"
#include "stats.h"   // [rc4l] cycle_t, the backend's clocks
#include "features/materials/materials.h"   // [rc4l] texture identity, frames, brightmaps and pixels
#include "r_sky.h"
#include "g_shared/a_sharedglobal.h"        // ASkyViewpoint: the sky that is a place, not a picture
#include "features/fixed64/computation/angle_interp_compute.h"   // InterpolateAngleBAM
#include "textures/textures.h"
#include "g_level.h"
#include "ShaderResourceVariable.h"
#include "r_utility.h"
#include "r_state.h"
#include "doomtype.h"
#include "i_system.h"
#include <math.h>
#include <algorithm>
#include <functional>
#include <stdio.h>
#include "m_png.h"
#ifdef ZX_ENABLE_REPLAY
#include "features/replay/zx_replay.h"   // [rc4l] the Vulkan instant-replay stream
#endif


EXTERN_CVAR(Int, r_fogmode)
EXTERN_CVAR(Bool, gl_wallmesh)
EXTERN_CVAR(Int, r_texture_filter)
EXTERN_CVAR(Float, skyoffset)
EXTERN_CVAR(Float, r_lights_size)
EXTERN_CVAR(Float, r_lights_intensity)
EXTERN_CVAR(Bool, r_lights_additive)
EXTERN_CVAR(Bool, r_mirrors)
EXTERN_CVAR(Bool, fua_dg_skyportal)
EXTERN_CVAR(Int, fua_dg_skyportal_scale)
EXTERN_CVAR(Float, fua_dg_skyportal_reach)

EXTERN_CVAR(Bool, fua_dg_animate)
EXTERN_CVAR(Bool, fua_dg_bindless)
EXTERN_CVAR(Bool, fua_dg_bindless_dyn)
EXTERN_CVAR(Int, fua_dg_cull)
EXTERN_CVAR(Bool, fua_dg_embed)
EXTERN_CVAR(Int, fua_dg_lightmode)
EXTERN_CVAR(Bool, fua_dg_loopcommits)
EXTERN_CVAR(Bool, fua_dg_mirrors_on)
EXTERN_CVAR(Bool, fua_dg_planemirrors)
EXTERN_CVAR(Bool, fua_dg_rtmirrors)
EXTERN_CVAR(Bool, fua_dg_stackportals)
EXTERN_CVAR(Bool, fua_dg_stackshow)
EXTERN_CVAR(Bool, fua_dg_standalone)
EXTERN_CVAR(Bool, fua_diligent_live)
EXTERN_CVAR(Bool, fua_vulkan)

// [rc4l] The camera pitch, which lives outside any header the backend already pulls in.
extern int viewpitch;

namespace zx { namespace hwrender {

// [rc4l] What the pass being drawn is, and the state that follows from which: one record every pass sets and every draw reads, in place of eight globals.
struct PassContext
{
	bool  mainView = false;          // [rc4l] the player's own view, the only one that dumps its order or paints plane reflections
	bool  inSkyPortal = false;       // [rc4l] the skybox portal rendering its own view, whose sky is part of the skybox image and is drawn
	bool  inCameraPass = false;      // [rc4l] a camera texture, whose sky quads are its own walk's
	int   lineMirror = -1;           // [rc4l] the line mirror whose reflection is being drawn, the one pass that shows its viewer, or -1
	int   mirrorView = -1;           // [rc4l] the mirror view (MirrorViewOf) whose walk's sprites, models and decals this pass draws, or -1
	int   stackSlot = -1;            // [rc4l] the stacked view being rendered, or -1
	float skyCamX = 0.f, skyCamY = 0.f, skyCamZ = 0.f;   // [rc4l] where the skybox camera stood, for the reach test in the world draw
	float clipPlane[4] = { 0.f, 0.f, 0.f, 0.f };   // [rc4l] a mirror's clip plane in mesh space (nx, ny, nz, d), all zero for none
	bool  gbufBound = false;         // [rc4l] drawing into the G-buffer, which a pipeline with one colour target cannot do
};
extern PassContext g_pass;

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
	// [rc4l] A wall's glow (MeshPiece::glowTop and the rest): two colours with their heights, and the ceiling and floor planes they are measured from.
	float glowTop[4], glowBottom[4], glowTopPlane[4], glowBottomPlane[4];
	// [rc4l] A player's damage tint (MeshPiece::tint): colour and strength, then its texture-V span and 1 when it glows.
	float tint[4], tintRange[4];
	float desaturation, scrollU, scrollV, texFlags;   // [rc4l] the colormap's desaturation, 0..1, which main.fp mixes every texel toward grey by, the texture shift a scroller added since the bake, and the piece's texture modes, styles and passes (ComputePieceTexFlags)
};

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
	int          walkId;   // [rc4l] a blended batch's one piece's PieceStackId, which the walk marks it reached by, or -1
};

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
	bool         hud;          // [rc4l] a HUD model's surface, whose blended pipelines write depth
	int          mirrorView;   // [rc4l] the mirror view whose walk alone drew it (MirrorCaptureView), or -1 for the frame's
	int          stackSlot;    // [rc4l] drawn only in this stacked view, whose walk took it, or -1 for every other pass
	int          onMirror;     // [rc4l] a decal on this line mirror's face, drawn over its surface by DrawMirrorFaceDecals, or -1
	float        cx, cy, cz;   // centroid, for the merged sort
	float        lo, hi;       // [rc4l] its height span, which sides of a translucent plane it is on
};

// [rc4l] The array's size on this device (ComputeMaterialSlotCapacity), which CompileShader hands every shader as FUA_MAT_SLOTS; 0 means a binding per material.
unsigned MaterialSlotCapacity();

// [rc4l] The walk slot the pass being drawn draws alone (ComputePassWalkSlot), which the scene shader culls by, or -1 for one that draws everything.
int PassWalkSlot();

struct MatSlot { const void *material; int translation; };
// [rc4l] The translation a brightmap's slot is filed under: no real one is this, so a material and its brightmap never share a slot.
static const int kBrightmapTranslation = -0x7fffffff;

// [rc4l] The scene as the backend holds it on the GPU: one owner for the buffers and batches every pass draws from.
struct SceneGeometry
{
	Diligent::RefCntAutoPtr<Diligent::IBuffer> vb;   // [rc4l] the material-sorted vertices
	int          verts = 0;
	TArray<SceneVertex> cpuVerts;                   // [rc4l] what the buffer is written from
	unsigned int vbCapacity = 0;                    // [rc4l] vertices the buffer holds, more than it draws, so a piece can be appended
	int          appendedVerts = 0;                 // [rc4l] how much of that slack has been spent since the last rebuild
	unsigned int builtLayoutGen = 0;                // [rc4l] the mesh layout generation the buffer was built for
	// [rc4l] The per-piece records the vertices index into, sized per level with slack like the vertex buffer.
	TArray<ScenePieceData> pieces;
	Diligent::RefCntAutoPtr<Diligent::IBuffer> pieceBuf;
	unsigned int pieceCapacity = 0;
	TArray<SceneBatch> batches;
	TArray<int>  blendBatches;                      // [rc4l] the translucent batches, in build order
	// [rc4l] The stacked views' subsector bits (ComputeMarkStackVisible), ready once this frame's are uploaded.
	Diligent::RefCntAutoPtr<Diligent::IBuffer> stackVisBuf;
	unsigned int stackVisCapacity = 0;
	bool         stackVisReady = false;
	// [rc4l] The player's sky holes (zx::levelmesh::SkyHoles), and how many this frame uploaded for the main pass to test.
	Diligent::RefCntAutoPtr<Diligent::IBuffer> skyHoleBuf;
	int          skyHoleCount = 0;
	TArray<DynRun> dynRuns;                         // [rc4l] this frame's sprite, model and decal draws
	bool         dynReady = false;
	unsigned int batchesGen = 0;                        // [rc4l] bumped when the batches are rebuilt or one is appended, for what is cached per batch
	TArray<zx::hwrender::TracedSprite> tracedSprites;   // [rc4l] this frame's sprites as a traced mirror's rays meet them
	std::vector<int> tracedSteps;                       // [rc4l] and how a ray goes from one mirror view to the next (ComputeMirrorSteps)
};
extern SceneGeometry g_scene;

// [rc4l] The world pipelines, one per blend mode and variant, and the two bindings the per-material path draws through.
struct ScenePipelines
{
	// [rc4l] One per WorldPipeline (ComputeWorldPipelineDesc): sprites never cull, since a billboard's winding is not promised, and decals carry GL's depth bias against the wall they lie flat on.
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> world[kWorldPipelines];
};
extern ScenePipelines g_pso;

// [rc4l] What the frame being drawn shares across its passes.
struct FrameState
{
	// [rc4l] Bumped once a frame and once per camera texture, so the jobs every pass needs run once however many passes draw the world.
	unsigned int serial = 1;
	// [rc4l] A camera texture's own field of view and shape, zero for the screen's, which is every ordinary frame.
	float fovOverride = 0.f, aspectOverride = 0.f;
	int   drawRepeat = 1;   // [rc4l] how many times the bench draws the world a frame
};
extern FrameState g_frame;

// [rc4l] What the passes counted, for fua_dg_dynstats and fua_frametimes to report.
struct SceneStats
{
	// [rc4l] Why a patch gave up and rebuilt the scene: a translucent piece that moved or has no slot, one whose blend left its batch's, or one whose material did, and the last one's texture.
	int     bailBlend = 0, bailBlendedSlot = 0, bailMaterial = 0;
	FString bailLast;
	int     geomUpdates = 0, geomRebuilds = 0;      // [rc4l] scene rebuilds this frame and since load, because the world moved
	unsigned int lastDirtyLo = 0, lastDirtyHi = 0;
	int     geomPatches = 0, geomPatchVerts = 0;    // [rc4l] patches and the vertices they moved, which tell nudged from rewritten
	int     geomAppends = 0, appendNoSlot = 0, appendResized = 0, slotRevived = 0;   // [rc4l] pieces put on the end, and why
	int     geomRepaints = 0, repaintRuns = 0;
	int     fRepaints = 0, fRuns = 0, fPatchVerts = 0, fPatches = 0;   // [rc4l] what THIS frame did, so a p99 can be attributed
	int     batchesDrawn = 0, worldDraws = 0;       // [rc4l] batches drawn, and the draw CALLS that took
	// [rc4l] World passes in the last presented frame, their CPU time and kinds, and its phase times: geometry, lights, clusters, constants, animation, draw.
	int     lastFramePasses = 0;
	double  lastFrameWorldMs = 0.0;
	FString lastFramePassList;
	double  lastFramePhase[6] = {};
	int     dynDraws = 0, dynTris = 0;              // [rc4l] the sorted list's entries, not what was submitted
	int     blendSubmits = 0;                       // [rc4l] what was actually submitted
	int     dynByBlend[4] = {};                     // [rc4l] per blend mode: opaque, translucent, additive, fuzz
	double  dynBuildMs = 0.0;                       // [rc4l] the dynamic stream's cost, which no other clock covers
	int     animSwaps = 0;                          // [rc4l] animation frame swaps since load
	int     dynBindless = 0, dynPerMaterial = 0, dynMirrorRuns = 0;
	int     dynSlotRefused = 0, dynSlotNoMaterial = 0, dynSlotSeen = 0, dynSlotMax = 0;
	int     dynSlotUnfilled = 0, dynSlotUnfilledTotal = 0;   // [rc4l] dynamic slots past the array's fill this frame and since load
	// [rc4l] Per pipeline: '-' untouched, 'x' the array is not there, 'f' filled, 'b' filled and bound.
	char    fillState[kWorldPipelines + 1] = {};
	FString fillNames[8];
	const void *fillViews[8] = {};
	int     fillCount = 0, remapUploads = 0, matSlotsWhite = 0;
};
extern SceneStats g_stats;

Diligent::IRenderDevice  *GetDevice();
Diligent::ITextureView   *GetMaterialSRV(const void *materialPtr, int translation);
Diligent::ITextureView   *GetBrightmapSRV(const void *materialPtr);
int MaterialCount();
bool MaterialIsMasked(const void *materialPtr);
Diligent::IDeviceContext *GetContext();
// [rc4l] A texture read back top-down as RGB the caller deletes, its channel order taken from its format; NULL with a reason in `report`.
unsigned char *ReadTextureRGB(Diligent::ITexture *tex, int &w, int &h, int &format, bool &bgra, FString &report);
// [rc4l] The same without waiting, for a device that cannot: tex copied now, and done called with its RGB on a later frame's PollLaterReadbacks; false when the copy could not be made.
bool ReadTextureLater(Diligent::ITexture *tex, std::function<void(const unsigned char *rgb, int w, int h)> done);
// [rc4l] Once a frame: hand each later read-back the GPU has finished to its caller.
void PollLaterReadbacks();
// [rc4l] Before a frame is drawn: a swapchain whose surface does not resize it (a browser's canvas) takes the surface's size.
void FollowBackendSurface();
// [rc4l] The screen wipe (dgwipe.cpp): the new screen taken from the frame just drawn, whether one is under way, and its mix drawn into the bound frame target.
void WipeTakeEnd();
bool WipeActive();
void WipeDraw(Diligent::IDeviceContext *ctx);
Diligent::ISwapChain     *GetSwapChain();
bool DiligentShowWindow(FString &report);
void *GetBackendWindow();
void Fua_SetBackendWindowSize(int w, int h);

// [rc4l] The texture cache (dgtexture.cpp) and the sky's fog (dgsky.cpp).
void ReleaseMaterials();
void GetSkyFog(float &r, float &g, float &b, float &density);

// [rc4l] The world pass and frame composition (dgscene.cpp).
bool CreateColorTarget(int w, int h, const char *name, Diligent::RefCntAutoPtr<Diligent::ITexture> &color);
bool CreateDepthTarget(int w, int h, const char *name, Diligent::RefCntAutoPtr<Diligent::ITexture> &depth);
zx::hwrender::BackendCamera CurrentCamera();
void DrawSceneOnce(bool present = true, bool pump = true);
// [rc4l] Whether the uploaded scene can be drawn: its geometry and pipelines are up.
bool SceneDrawable();
// [rc4l] Record, roll the frame's pass stats, present, and keep the window in step; the world's frames and the 2D-only ones alike.
void PresentFrame(Diligent::IDeviceContext *ctx, Diligent::ISwapChain *swap, bool pump);
void PhaseReport(FString &out);

// [rc4l] Pipelines and material bindings (dgpipelines.cpp).
int AnimationOf(const void *material);
Diligent::IPipelineState *BatchPSO(int blend);
Diligent::IShaderResourceBinding *BatchSRB(SceneBatch &b);
bool BindlessActive();
void CommitInLoop(Diligent::IDeviceContext *ctx, zx::hwrender::LoopCommits &loop, Diligent::IShaderResourceBinding *srb);
void DirtyMaterialSlots();
bool EnsureScenePipeline(FString &err);
void FlushTextureCache();
// [rc4l] Drop one material's per-material bindings and the batches' copies, so its next draw binds what it shows now.
void ForgetMaterialSRBs(const void *material);
void MaterialSRBCounts(int &cached, int &batch);
int MaterialSlotCount();
int MaterialSlotFor(const void *material, int translation);
void RefreshBindless();
void ReleaseBatchSRBs();
void ReleaseMaterialSRBs();
void ReleaseScenePipelines();
void ReleaseWorldSRBs();
void ResetMaterialSlots();
void SlotReport(int n, FString &out);
Diligent::IShaderResourceBinding *WorldSRB(Diligent::IPipelineState *pso);
const TArray<MatSlot> &MaterialSlots();
int FilledSlotCount();
void MarkMaterialSlotsCurrent();

// [rc4l] The per-frame stream (dgdynamic.cpp).
void BuildDynamic(Diligent::IDeviceContext *ctx);
void DrawBlended(Diligent::IDeviceContext *ctx);
void DrawSolidDyn(Diligent::IDeviceContext *ctx);
// [rc4l] The decals on line mirror `line`'s face, over the surface just drawn, in the order its side holds them.
void DrawMirrorFaceDecals(Diligent::IDeviceContext *ctx, int line);
// [rc4l] Stereo 3D (dgstereo.cpp): vr_mode's eyes, the one being drawn (0 the middle), how it moves the camera and leans BuildMVP's frustum, and an anaglyph's eyes kept and blended.
zx::hwrender::StereoMode CurrentStereoMode();
void SetStereoEye(float shift);
void ShiftCameraForEye(zx::hwrender::BackendCamera &c, float &ndcShift);
void KeepEyePicture(Diligent::IDeviceContext *ctx, int eye);
void BlendEyes(Diligent::IDeviceContext *ctx, const zx::hwrender::StereoMode &mode);
// [rc4l] The hitbox view's boxes over the player's view (dglines.cpp), before the 2D.
void DrawHitboxes(Diligent::IDeviceContext *ctx);

// [rc4l] The scene buffer (dgupload.cpp).
void FillPieceData(const zx::levelmesh::MeshPiece &p, int translation, ScenePieceData &pd, bool inMesh);
// [rc4l] A piece's material and brightmap slots and whether its texture animates, which may give a material a slot, so they are found on one thread before records are filled on many.
struct PieceSlotSet { float mat, bright; bool animates; };
PieceSlotSet PieceSlots(const zx::levelmesh::MeshPiece &p, int translation, bool inMesh);
// [rc4l] FillPieceData with the slots already found, which only reads, so lanes can run it.
void FillPieceDataWith(const zx::levelmesh::MeshPiece &p, const PieceSlotSet &slots, ScenePieceData &pd, bool inMesh);
void ForgetBatchSRBs();
void RefreshMovedGeometry(Diligent::IDeviceContext *ctx);
void ReserveDynamicSlots();
bool SceneUpload(FString &report);

// [rc4l] Mirrors, reflective planes and stacked views (dgviews.cpp).
void CollectMirrors();
void DrawPlaneMirrorSurfaces(Diligent::IDeviceContext *ctx);
void DrawStackSurfaces(Diligent::IDeviceContext *ctx);
bool EnsureAccelerationStructure();
bool EnsureMirrorResources();
bool RayTracingAvailable();
int RecollectMirrors();
void ReleaseAccelerationStructures();
void ReleaseLineMirrorPipeline();
void ReleaseMirrorBinding();
void ReleasePlaneMirrors();
void ReleaseStackPortals();
// [rc4l] Every line mirror the player sees, and the mirrors in their reflections to r_mirror_recursions, drawn into pictures before the frame; DrawLineMirrorSurfaces paints each pass's own before its translucent pass.
void RenderMirrors(Diligent::IDeviceContext *ctx);
void DrawLineMirrorSurfaces(Diligent::IDeviceContext *ctx);
void RenderPlaneMirrors(Diligent::IDeviceContext *ctx);
void RenderStackPortals(Diligent::IDeviceContext *ctx);
void ShowStackView(Diligent::IDeviceContext *ctx);
void StackInfo(FString &out);
void UploadWalkVisibility(Diligent::IDeviceContext *ctx);
void WriteSceneConstants(float *cb, const float *clip, float targetW, float targetH, float materialSlots);

// [rc4l] Camera textures (dgcamera.cpp).
void ReleaseCameraTargets();

// [rc4l] The frame driver (dgframe.cpp).
void LiveFrame();
void RecordPresentInterval();
// [rc4l] The back buffer as drawn and not yet presented, as top-down RGB the caller deletes; NULL with the reason in report.
unsigned char *ReadBackBuffer(int &w, int &h, int &format, bool &bgra, FString &report);
bool StandaloneActive();

#ifdef ZX_ENABLE_REPLAY
// [rc4l] Measurements (dgdiag.cpp).
void CaptureReplayFrame(Diligent::IDeviceContext *ctx, Diligent::ISwapChain *swap);
#endif


// [rc4l] The world camera's matrix and the scene constant buffer every pass writes before it draws.
extern float g_mvp[16];
extern Diligent::RefCntAutoPtr<Diligent::IBuffer> g_cb;
void BuildMVP(float *m);
void DrawWorld(Diligent::IDeviceContext *ctx);
// [rc4l] A GL vertex (x, y, height) as the mesh stores it (x, height, y), tagged with its piece.
SceneVertex ToSceneVertex(const zx::levelmesh::MeshVertex &sv, unsigned int pieceIndex);
Diligent::ITextureView *EnsureSceneDepth();
bool CreateViewTextures(int w, int h, const char *name,
	Diligent::RefCntAutoPtr<Diligent::ITexture> &color, Diligent::RefCntAutoPtr<Diligent::ITexture> &depth);
bool CreatePictureBinding(Diligent::IPipelineState *pso, const char *var, Diligent::ITexture *tex,
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> &srb);

// [rc4l] The sky (dgsky.cpp).
void EnsureSky();
// [rc4l] The marker a scene shader takes GLDEFS' hardware shaders at, and the block that replaces it (dgpipelines.cpp), each shader checked to compile alone and all of them dropped if together they break the scene shaders.
#define kUserShaderMarker "//FUA_USER_SHADERS"
const std::string &UserShaderBlock();
// [rc4l] Each sky other than the level's that the player's sky surfaces show this frame, drawn into a picture of its own before the frame, which DrawSkySurfaces paints them from.
void RenderSkyLooks(Diligent::IDeviceContext *ctx);
// [rc4l] The same for another view about to be drawn at w by h from the eye in g_mvp (a camera texture, a stacked view by slot, a mirror), before its target is bound; ForgetViewSkyLooks once it is drawn.
void RenderViewSkyLooks(Diligent::IDeviceContext *ctx, bool cameraPass, int stackSlot, bool mirrorPass, int w, int h);
void ForgetViewSkyLooks();
bool EnsureSkyPortalResources();
bool RenderSkyPortal(Diligent::IDeviceContext *ctx);
void DrawSkyBackground(Diligent::IDeviceContext *ctx);
void DrawSkySurfaces(Diligent::IDeviceContext *ctx);
void ReleaseSkyPipelines();
void ResetSkyForLevel();
void GetSkyConstants(float &angle, float &xScale, float &vScale, unsigned char &capR, unsigned char &capG, unsigned char &capB);
int SkyDraws();
Diligent::IPipelineState *SkyPortalPicturePSO();
bool CreatePicturePSO(const char *name, const char *ps, Diligent::FILTER_TYPE filter, Diligent::TEXTURE_FORMAT dsvFormat,
	Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso);
// [rc4l] CreatePicturePSO with `count` pictures named in `pictures`, each mutable and sampled with filter; any other resource of ps is static.
bool CreatePicturesPSO(const char *name, const char *ps, Diligent::FILTER_TYPE filter, Diligent::TEXTURE_FORMAT dsvFormat,
	const char *const *pictures, int count, Diligent::RefCntAutoPtr<Diligent::IPipelineState> &pso);
Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateScreenSurfacePSO(const char *name,
	Diligent::SHADER_RESOURCE_VARIABLE_TYPE texType, bool depthOnly = false);
bool BindScreenSurfaces(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const TArray<SceneVertex> &sv, Diligent::IPipelineState *pso, const char *name);
void DrawScreenSurfaceRun(Diligent::IDeviceContext *ctx, Diligent::IShaderResourceBinding *srb, unsigned int first, unsigned int count);

}} // namespace zx::hwrender

#endif // ZX_DGSCENE_INTERNAL_H
