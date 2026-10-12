// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the Vulkan backend's own files share and nothing outside them includes: their headers, the pass record, the scene vertex, and the scene state and helpers split across dgscene.cpp and its passes.

#ifndef ZX_DGSCENE_INTERNAL_H
#define ZX_DGSCENE_INTERNAL_H

#include "c_dispatch.h"
#include "c_console.h"


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
#include "features/hwrender/computation/batch_compute.h"   // [rc4l] ComputeStaysInBatch, for patching animated surfaces
#include "features/hwrender/computation/passname_compute.h"   // [rc4l] ComputeWorldPassName, for the per-pass timings
#include "features/hwrender/computation/mirrorsheen_compute.h"   // [rc4l] GL's sheen over a line mirror
#include "features/hwrender/computation/desaturate_compute.h"   // [rc4l] a sector's desaturation, as main.fp applies it
#include "features/levelmesh/computation/wallbatch_compute.h"   // [rc4l] ComputeFanTriangleVertex, a mirror quad split as GL fans it
#include "features/hwrender/hud2d.h"
#include "features/horizon/computation/horizon_compute.h"   // [rc4l] ComputeQuadMayBeVisible for mirrors out of sight, ComputeEyeFromViewProjection for the horizon sky
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
#ifdef ZX_ENABLE_REPLAY
#include "features/replay/zx_replay.h"   // [rc4l] the Vulkan instant-replay stream
#endif


EXTERN_CVAR(Int, gl_fogmode)
EXTERN_CVAR(Bool, gl_wallmesh)
EXTERN_CVAR(Int, gl_texture_filter)
EXTERN_CVAR(Float, skyoffset)
EXTERN_CVAR(Float, gl_lights_size)
EXTERN_CVAR(Float, gl_lights_intensity)
EXTERN_CVAR(Bool, gl_lights_additive)
EXTERN_CVAR(Bool, gl_mirrors)
EXTERN_CVAR(Bool, fua_dg_skyportal)
EXTERN_CVAR(Int, fua_dg_skyportal_scale)
EXTERN_CVAR(Float, fua_dg_skyportal_reach)

namespace zx { namespace hwrender {

// [rc4l] What the pass being drawn is, and the state that follows from which: one record every pass sets and every draw reads, in place of eight globals.
struct PassContext
{
	bool  mainView;          // [rc4l] the player's own view, the only one that dumps its order or paints plane reflections
	bool  inSkyPortal;       // [rc4l] the skybox portal rendering its own view, whose sky is part of the skybox image and is drawn
	bool  inCameraPass;      // [rc4l] a camera texture, whose sky quads are its own walk's
	int   lineMirror;        // [rc4l] the line mirror whose reflection is being drawn, the one pass that shows its viewer, or -1
	int   stackSlot;         // [rc4l] the stacked view being rendered, or -1
	float skyCamX, skyCamY, skyCamZ;   // [rc4l] where the skybox camera stood, for the reach test in the world draw
	float clipPlane[4];      // [rc4l] a mirror's clip plane in mesh space (nx, ny, nz, d), all zero for none
	bool  gbufBound;         // [rc4l] drawing into the G-buffer, which a pipeline with one colour target cannot do
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

// [rc4l] The world camera's matrix and the scene constant buffer every pass writes before it draws.
extern float g_mvp[16];
extern Diligent::RefCntAutoPtr<Diligent::IBuffer> g_cb;
void BuildMVP(float *m);
void DrawWorld(Diligent::IDeviceContext *ctx);
// [rc4l] A GL vertex (x, y, height) as the mesh stores it (x, height, y), tagged with its piece.
SceneVertex ToSceneVertex(const FFlatVertex &sv, unsigned int pieceIndex);
Diligent::ITextureView *EnsureSceneDepth();
bool CreateViewTextures(int w, int h, const char *name,
	Diligent::RefCntAutoPtr<Diligent::ITexture> &color, Diligent::RefCntAutoPtr<Diligent::ITexture> &depth);
bool CreatePictureBinding(Diligent::IPipelineState *pso, const char *var, Diligent::ITexture *tex,
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> &srb);

// [rc4l] The sky (dgsky.cpp).
void EnsureSky();
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
Diligent::RefCntAutoPtr<Diligent::IPipelineState> CreateScreenSurfacePSO(const char *name,
	Diligent::SHADER_RESOURCE_VARIABLE_TYPE texType, bool depthOnly = false);
bool BindScreenSurfaces(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const TArray<SceneVertex> &sv, Diligent::IPipelineState *pso, const char *name);
void DrawScreenSurfaceRun(Diligent::IDeviceContext *ctx, Diligent::IShaderResourceBinding *srb, unsigned int first, unsigned int count);

}} // namespace zx::hwrender

#endif // ZX_DGSCENE_INTERNAL_H
