// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The Vulkan backend's world pass: the scene's records, frame prep, the world draw and the frame it composes; dgscene_internal.h lists the files the rest is split into.

#include "features/hwshaders/computation/hwshader_compute.h"   // [rc4l] ComputeSpliceShaderBlock
#include "features/hwrender/computation/framepacing_compute.h"
#include "features/hwrender/computation/shaderdump_compute.h"
#include "features/hwrender/computation/layoutvars_compute.h"
#include "features/hwrender/computation/shadercache_compute.h"   // [Doruk-Aksoy] ComputeCachedShader
#include "features/hwrender/computation/spirvoptimize_compute.h"   // [Doruk-Aksoy] ComputeOptimizeSpirv
#include "features/mainloop/mainloop.h"   // [rc4l] Stepped, a browser pacing the frames
#include "features/wipe/wipetrace.h"   // [rc4l] fua_wipe_trace
#include "cmdlib.h"   // [rc4l] CreatePath
#include "features/framephase/framephase.h"   // [rc4l] the backend, upload, lights and present phases
#include "c_dispatch.h"
#include "c_console.h"

#ifdef FUA_DILIGENT

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/telemetry/telemetry.h"
#include "features/hwrender/computation/uploadtally_compute.h"
#include <mutex>
#include "features/hwrender/diligent/dggputimer.h"   // [rc4l] gpu/frame_ms, gpu/world_ms, gpu/2d_ms
#include "features/hwrender/diligent/dgdevice.h"   // [rc4l] PagePaced
#include "features/viewsetup/viewsetup.h"   // [MobiusU9B] the view's lens
#include "features/frame-limit/framelimit.h"   // [rc4l] WaitBeforePresent
#include "features/draw2d/record.h"   // [Doruk-Aksoy] ViewQuads, the 2D a post-process scene shader runs over

// [rc4l] Declared outside the namespace: EXTERN_CVAR builds a name from the identifier, so a
// namespace-qualified one resolves to a symbol the engine never defines and fails only at link time.
EXTERN_CVAR(Int, r_fogmode)
// [rc4l] fua_vulkan turns this on itself -- see AutoSetupForLevel.
EXTERN_CVAR(Bool, gl_wallmesh)
// [rc4l] The engine's texture filter mode; the backend mirrors it. See FillSamplerFromEngine.
EXTERN_CVAR(Int, r_texture_filter)
// [rc4l] The sky's own vertical nudge, both the per-texture one and the global testing cvar.
EXTERN_CVAR(Float, skyoffset)

EXTERN_CVAR(Float, r_lights_size)
EXTERN_CVAR(Float, r_lights_intensity)
EXTERN_CVAR(Bool, r_lights_additive)
EXTERN_CVAR(Bool, r_mirrors)
EXTERN_CVAR(Int, screenblocks)   // [rc4l] the view window the player's view is framed in
EXTERN_CVAR(Bool, vid_vsync)   // [rc4l] picks the present interval, FIFO when on and mailbox when off

// [rc4l] Whether the backend presents in step with the display, as features/telemetry's frame/vsync.
static double VsyncOn() { return vid_vsync ? 1.0 : 0.0; }
static const bool s_vsyncCounted = (zx::telemetry::Register(zx::telemetry::CounterSpec{ "frame/vsync", "on", zx::telemetry::CounterKind::Gauge, 500 }, VsyncOn), true);

// [rc4l] 0 flat multiply (pre-lighting-port behaviour), 1 the ported equation, 2 depth as grey,
// 3 depth contours, 4 vertex colour only. Live per frame -- no rebake needed to switch.
CVAR(Int, fua_dg_lightmode, 1, CVAR_ARCHIVE)
namespace zx { namespace hwrender { PassContext g_pass; }}
using zx::hwrender::g_pass;
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
// [rc4l] Sprites in traced mirrors: each a box the ray query offers around the quad a mirror's walk faced to that reflection's eye; 0 leaves them out, to measure what they cost.
CVAR(Bool, fua_dg_rtsprites, true, CVAR_ARCHIVE)
// Planar mirror reflections. On by default; 0 takes the whole pass out, for bisecting a GPU hang.
CVAR(Bool, fua_dg_mirrors_on, true, CVAR_ARCHIVE)
// [rc4l] Reflective planes get their reflections, each a render of the world, every plane the level has; 0 turns them off.
CVAR(Bool, fua_dg_planemirrors, true, CVAR_ARCHIVE)
// [rc4l] Stacked-sector views (GL's GLSectorStackPortal) get rendered, each a render of the world, as many as the level has portals; 0 turns them off.
CVAR(Bool, fua_dg_stackportals, true, CVAR_ARCHIVE)
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
// [rc4l] Collapse adjacent batches into one draw. Only ever possible with bindless on, and separable
// from it so "the array is wrong" and "the merge is wrong" can be told apart in one run.
CVAR(Bool, fua_dg_mergedraws, true, 0)
// [rc4l] Let the material array follow animated textures. Separable because following them means
// rebuilding thirteen bindings, and a binding the GPU is still reading is its own kind of wrong.
CVAR(Bool, fua_dg_bindless_anim, true, 0)

// [rc4l] The shader spells 32, 18 and 24 out since a GLSL string cannot read a C++ constant, so a grid change stops the build until the shader changes with it.
static_assert(zx::hwrender::kClusterTilesX == 32 && zx::hwrender::kClusterTilesY == 18 && zx::hwrender::kClusterSlices == 24,
	"the cluster grid changed: update the 32, 18 and 24 in FUA_LIGHT_GLSL to match.");

// [rc4l] Which of the three ways a mark is drawn -- see projdecals.h. Declared at file scope, not
// inside zx::hwrender: a cvar lives in the global namespace, and an EXTERN_CVAR inside a namespace
// declares a DIFFERENT symbol that links against nothing.

// [rc4l] A directory every composed shader is written into as it compiles, to check each against the WebGPU conversion off the engine.
// [rc4l] A script cannot choose where shaders are dumped (features/command-safety).
CVAR(String, fua_dg_shaderdump, "", CVAR_NOSETBYACS)

// [Doruk-Aksoy] Whether a shader's SPIR-V goes through SPIRV-Tools' optimizer before its pipeline is made: 1 always, 0 never, -1 only on WebGPU (ComputeOptimizeSpirv).
CVAR(Int, fua_dg_spirv_optimize, -1, CVAR_ARCHIVE)

namespace zx { namespace hwrender {

// [rc4l] Backend chatter, off by default: here, when the scene targets are rebuilt and at what size.
EXTERN_CVAR(Bool, fua_dg_verbose)

Diligent::RefCntAutoPtr<Diligent::IBuffer>        g_cb;
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
Diligent::ITextureView *SceneDepthSRV();
// The same texture as a depth target, for putting the attachment back after a pass that samples it.
Diligent::ITextureView *SceneDepthDSV();
static int g_sceneDepthW = 0, g_sceneDepthH = 0;

float g_mvp[16];
SceneGeometry  g_scene;
ScenePipelines g_pso;
FrameState     g_frame;
SceneStats     g_stats;

// [rc4l] World passes in the frame being drawn and the last presented one, with their CPU time, since a frame draws the world once per mirror, stacked view, sky portal and camera texture.
static int g_framePasses = 0;
static double g_frameWorldMs = 0.0;
// [rc4l] Each pass of the frame by kind and CPU time, e.g. "sky 3.10 main 1.02".
static FString g_framePassList;
// [rc4l] The frame's phase times summed over its passes: geometry, lights, clusters, constants, animation, draw.
static double g_framePhase[6] = { 0 };
static unsigned int g_preparedSerial = 0;
// [rc4l] Off runs the frame-wide jobs again in every pass, the old cost, for an A/B in one session.
CVAR(Bool, fua_dg_frameprep, true, 0)

static void DumpShader(Diligent::SHADER_TYPE type, const char *name, const std::string &text)
{
	const char *dir = fua_dg_shaderdump;
	if (*dir == '\0') return;
	CreatePath(dir);
	FString path;
	path.Format("%s/%s", dir, ComputeShaderDumpName(name, type == Diligent::SHADER_TYPE_PIXEL).c_str());
	FILE *f = fopen(path.GetChars(), "wb");
	if (f == NULL) { Printf("fua_dg_shaderdump: cannot write %s\n", path.GetChars()); return; }
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

// [rc4l] Shaders and pipelines made since the start as features/telemetry gauges, since each is a compile the driver may keep and a frame may wait on.
static double s_shadersMade = 0.0, s_pipelinesMade = 0.0;
static double ShadersMade() { return s_shadersMade; }
static double PipelinesMade() { return s_pipelinesMade; }
static const bool s_madeCounted = (
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/made/shaders", "shaders", zx::telemetry::CounterKind::Gauge, 500 }, ShadersMade),
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/made/pipelines", "pipelines", zx::telemetry::CounterKind::Gauge, 500 }, PipelinesMade),
	true);

// [Doruk-Aksoy] Every shader compiled, by stage and final text, so a level's pipelines made again take the last level's shaders instead of compiling them again.
static zx::hwrender::ShaderCache<Diligent::RefCntAutoPtr<Diligent::IShader>> s_shaderCache;
static double ShaderHits() { return (double)s_shaderCache.hits; }
static const bool s_shaderHitsCounted = (zx::telemetry::Register(
	zx::telemetry::CounterSpec{ "gpu/made/shader_hits", "shaders", zx::telemetry::CounterKind::Gauge, 500 }, ShaderHits), true);

void ReleaseShaderCache() { s_shaderCache.kept.clear(); }

std::string ShaderText(const char *source, int userShader)
{
	// [rc4l] Every shader learns the material array's size on this device, and whether it has the array at all.
	// [Doruk-Aksoy] A GLDEFS shader's program is told so, which its vertex stage hands the model matrix on for.
	std::string text = zx::hwrender::ComputeShaderWithDefines(source, zx::hwrender::ComputeMaterialShaderDefines(MaterialSlotCapacity()) +
		(userShader >= 0 ? "#define FUA_USER_SHADER 1\n" : ""));
	// [rc4l] A scene shader takes GLDEFS' hardware shaders at its marker; nothing else has one.
	// [Doruk-Aksoy] ...nor the 2D layer's, at its own.
	for (const zx::hwshaders::ProgramKind kind : { zx::hwshaders::kSceneProgram, zx::hwshaders::k2DProgram })
		if (text.find(zx::hwshaders::ComputeProgramMarker(kind)) != std::string::npos)
			text = zx::hwshaders::ComputeSpliceShaderBlock(text, zx::hwshaders::ComputeProgramMarker(kind), ProgramBlock(kind, userShader));
	return text;
}

Diligent::RefCntAutoPtr<Diligent::IShader> CompileShaderText(Diligent::SHADER_TYPE type, const char *name, const std::string &text, bool async)
{
	Diligent::RefCntAutoPtr<Diligent::IShader> shader;
	auto *dev = GetDevice();
	if (dev == NULL) return shader;
	Diligent::ShaderCreateInfo ci;
	ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
	// [rc4l] Each texture's sampler is declared apart as <texture>_sampler, since WGSL has no combined sampler, and this pairs the two for the bindings and immutable samplers named by texture.
	ci.Desc.UseCombinedTextureSamplers = true;
	ci.Desc.ShaderType = type;
	ci.Desc.Name = name;
	ci.Source = text.c_str();
	// [Doruk-Aksoy] Asked for, compiled on the device's pool and waited for later; ignored by a device without the pool.
	if (async) ci.CompileFlags |= Diligent::SHADER_COMPILE_FLAG_ASYNCHRONOUS;
	// [Doruk-Aksoy] Off skips SPIRV-Tools' performance passes after glslang, which the driver's own compile of the pipeline repeats.
	if (!zx::hwrender::ComputeOptimizeSpirv(fua_dg_spirv_optimize, dev->GetDeviceInfo().Type == Diligent::RENDER_DEVICE_TYPE_WEBGPU))
		ci.ShaderOptimizationLevel = Diligent::SHADER_OPTIMIZATION_LEVEL_DISABLED;
	ZX_ZONE("gpu/make_shader");   // [rc4l] a shader compiled
	dev->CreateShader(ci, &shader);
	s_shadersMade += 1.0;
	NoteMadeInPlay("shader", name);
	return shader;
}

static Diligent::RefCntAutoPtr<Diligent::IShader> CompileShaderWith(Diligent::SHADER_TYPE type, const char *name, const char *source, int userShader, bool async)
{
	if (GetDevice() == NULL) return Diligent::RefCntAutoPtr<Diligent::IShader>();
	const std::string text = ShaderText(source, userShader);
	return zx::hwrender::ComputeCachedShader(s_shaderCache, (int)type, text, [&]() {
		DumpShader(type, name, text);
		return CompileShaderText(type, name, text, async);
	});
}

Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::SHADER_TYPE type, const char *name, const char *source, int userShader)
{
	Diligent::RefCntAutoPtr<Diligent::IShader> shader = CompileShaderWith(type, name, source, userShader, false);
	// [Doruk-Aksoy] One still compiling in the background is waited for, and one that failed there answers null as a failed compile here does.
	if (shader && shader->GetStatus(true) != Diligent::SHADER_STATUS_READY) return Diligent::RefCntAutoPtr<Diligent::IShader>();
	return shader;
}

Diligent::RefCntAutoPtr<Diligent::IShader> CompileShaderAsync(Diligent::SHADER_TYPE type, const char *name, const char *source, int userShader)
{
	return CompileShaderWith(type, name, source, userShader, true);
}

void CreatePipeline(const Diligent::GraphicsPipelineStateCreateInfo &pci, Diligent::IPipelineState **pso)
{
	const Diligent::PipelineResourceLayoutDesc &layout = pci.PSODesc.ResourceLayout;
	std::vector<zx::hwrender::LayoutVar> vars;
	for (Diligent::Uint32 i = 0; i < layout.NumVariables; i++)
		vars.push_back({ (unsigned)layout.Variables[i].ShaderStages, layout.Variables[i].Name, (int)layout.Variables[i].Type, (unsigned)layout.Variables[i].Flags });
	std::vector<zx::hwrender::StageBuffer> buffers;
	for (Diligent::IShader *shader : { pci.pVS, pci.pPS })
	{
		if (shader == nullptr) continue;
		for (Diligent::Uint32 r = 0; r < shader->GetResourceCount(); r++)
		{
			Diligent::ShaderResourceDesc d;
			shader->GetResourceDesc(r, d);
			if (d.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_SRV || d.Type == Diligent::SHADER_RESOURCE_TYPE_BUFFER_UAV)
				buffers.push_back({ (unsigned)shader->GetDesc().ShaderType, d.Name });
		}
	}
	vars = zx::hwrender::ComputeFlagStorageBuffers(vars, buffers, (int)layout.DefaultVariableType,
		(unsigned)Diligent::SHADER_VARIABLE_FLAG_NO_DYNAMIC_BUFFERS);
	std::vector<Diligent::ShaderResourceVariableDesc> descs;
	for (const zx::hwrender::LayoutVar &v : vars)
		descs.push_back(Diligent::ShaderResourceVariableDesc((Diligent::SHADER_TYPE)v.stages, v.name.c_str(),
			(Diligent::SHADER_RESOURCE_VARIABLE_TYPE)v.type, (Diligent::SHADER_VARIABLE_FLAGS)v.flags));
	Diligent::GraphicsPipelineStateCreateInfo flagged = pci;
	flagged.PSODesc.ResourceLayout.Variables = descs.empty() ? nullptr : descs.data();
	flagged.PSODesc.ResourceLayout.NumVariables = (Diligent::Uint32)descs.size();
	ZX_ZONE("gpu/make_pipeline");   // [rc4l] a pipeline made, which the driver may compile then
	GetDevice()->CreateGraphicsPipelineState(flagged, pso);
	s_pipelinesMade += 1.0;
	NoteMadeInPlay("pipeline", pci.PSODesc.Name);
}

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
// [rc4l] A GL vertex (x, y, height) as the mesh stores it (x, height, y), tagged with its piece: the one conversion the full build, a patch and every per-frame stream share.
SceneVertex ToSceneVertex(const zx::levelmesh::MeshVertex &sv, unsigned int pieceIndex)
{
	SceneVertex dv;
	dv.x = sv.x; dv.y = sv.z; dv.z = sv.y;
	dv.u = sv.u; dv.v = sv.v;
	dv.pieceIndex = (float)pieceIndex;
	return dv;
}

// [rc4l] A w by h picture in the swapchain's colour format, drawn into and then sampled.
bool CreateColorTarget(int w, int h, const char *name, Diligent::RefCntAutoPtr<Diligent::ITexture> &color, Diligent::TEXTURE_FORMAT format)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || w <= 0 || h <= 0) return false;
	FString colorName;
	colorName.Format("%s colour", name);
	Diligent::TextureDesc td;
	td.Name = colorName.GetChars();
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = (Diligent::Uint32)w; td.Height = (Diligent::Uint32)h;
	td.Format = format != Diligent::TEX_FORMAT_UNKNOWN ? format : swap->GetDesc().ColorBufferFormat;
	td.MipLevels = 1;
	td.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
	color.Release();
	CreateDeviceTexture(td, nullptr, &color);
	return color.RawPtr() != nullptr;
}

// [rc4l] A w x h colour target the shaders can read and its depth buffer, in the swapchain's formats, for a view rendered off screen; false if either cannot be made.
bool CreateViewTextures(int w, int h, const char *name,
	Diligent::RefCntAutoPtr<Diligent::ITexture> &color, Diligent::RefCntAutoPtr<Diligent::ITexture> &depth, Diligent::TEXTURE_FORMAT format)
{
	return CreateColorTarget(w, h, name, color, format) && CreateDepthTarget(w, h, name, depth);
}

// [rc4l] A w by h depth buffer in the swapchain's depth format, for a view rendered off screen.
bool CreateDepthTarget(int w, int h, const char *name, Diligent::RefCntAutoPtr<Diligent::ITexture> &depth)
{
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap || w <= 0 || h <= 0) return false;
	FString depthName;
	depthName.Format("%s depth", name);
	Diligent::TextureDesc dd;
	dd.Name = depthName.GetChars();
	dd.Type = Diligent::RESOURCE_DIM_TEX_2D;
	dd.Width = (Diligent::Uint32)w; dd.Height = (Diligent::Uint32)h;
	dd.Format = swap->GetDesc().DepthBufferFormat;
	dd.MipLevels = 1;
	// [rc4l] Readable too, for the decal pass of the view it is drawn with.
	dd.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
	depth.Release();
	CreateDeviceTexture(dd, nullptr, &depth);
	return depth.RawPtr() != nullptr;
}

// [rc4l] A binding on pso with its pixel-stage texture var reading tex, the one way a picture drawn off screen is handed to the pass that paints it; false if pso makes none.
bool CreatePictureBinding(Diligent::IPipelineState *pso, const char *var, Diligent::ITexture *tex,
	Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> &srb)
{
	srb.Release();
	pso->CreateShaderResourceBinding(&srb, true);
	if (!srb) return false;
	if (auto *v = srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, var))
		v->Set(tex->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	return true;
}

// [rc4l] The most one write sends, under either device's upload page, so writes share pages rather than each taking one its own size.
static const size_t kUploadPageBytes = 1 << 20;

// [rc4l] The flush budget while an UploadFlushing lives on a browser's device, 0 otherwise, and what has gone since the last flush.
static size_t g_uploadFlushBudget = 0;
static size_t g_uploadSinceFlush = 0;

UploadFlushing::UploadFlushing()
{
	Diligent::IRenderDevice *dev = GetDevice();
	g_uploadFlushBudget = zx::hwrender::ComputeUploadFlushBudget(dev != NULL, dev != NULL && dev->GetDeviceInfo().Type == Diligent::RENDER_DEVICE_TYPE_WEBGPU);
	g_uploadSinceFlush = 0;
}

UploadFlushing::~UploadFlushing()
{
	g_uploadFlushBudget = 0;
}

void NoteUploadSent(Diligent::IDeviceContext *ctx, size_t bytes)
{
	if (zx::hwrender::ComputeUploadFlushDue(g_uploadSinceFlush, bytes, g_uploadFlushBudget)) ctx->Flush();
}

void UpdateBufferIfChanged(Diligent::IDeviceContext *ctx, Diligent::IBuffer *buf, zx::hwrender::UploadShadow &shadow, size_t bytes, const void *data)
{
	if (zx::hwrender::ComputeUploadChanged(shadow, buf->GetUniqueID(), data, bytes)) UpdateBufferInPages(ctx, buf, 0, bytes, data);
	else ZX_COUNT("gpu/upload_spared", "bytes", (double)bytes);
}

// [rc4l] What each buffer has been sent since gpu/upload/ was last read.
static std::mutex g_uploadTallyLock;
static zx::hwrender::UploadTally g_uploadTally;

static void UploadBreakdown(std::vector<zx::telemetry::Reading> &out)
{
	std::lock_guard<std::mutex> lock(g_uploadTallyLock);
	for (const auto &b : zx::hwrender::ComputeUploadTallyTake(g_uploadTally)) out.push_back({ "gpu/upload/" + b.first, "bytes", b.second });
}

static const bool s_uploadBreakdown = (zx::telemetry::RegisterBreakdown("gpu/upload/", UploadBreakdown, false), true);

void SendToBuffer(Diligent::IDeviceContext *ctx, Diligent::IBuffer *buf, unsigned long long offset, size_t bytes, const void *data)
{
	ctx->UpdateBuffer(buf, offset, bytes, data, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ZX_COUNT("gpu/upload_bytes", "bytes", (double)bytes);
	std::lock_guard<std::mutex> lock(g_uploadTallyLock);
	zx::hwrender::ComputeUploadTallyAdd(g_uploadTally, buf->GetDesc().Name, (double)bytes);
}

void NoteUploadFrame()
{
	std::lock_guard<std::mutex> lock(g_uploadTallyLock);
	zx::hwrender::ComputeUploadTallyFrame(g_uploadTally);
}

void UpdateBufferInPages(Diligent::IDeviceContext *ctx, Diligent::IBuffer *buf, unsigned long long offset, size_t bytes, const void *data)
{
	zx::hwrender::ComputeUploadChunks(bytes, kUploadPageBytes, [&](size_t at, size_t n)
	{
		SendToBuffer(ctx, buf, offset + at, n, (const char *)data + at);
		if (zx::hwrender::ComputeUploadFlushDue(g_uploadSinceFlush, n, g_uploadFlushBudget)) ctx->Flush();
	});
}

bool EnsureGrowingBuffer(Diligent::RefCntAutoPtr<Diligent::IBuffer> &buf, unsigned int &capacity, unsigned int count, unsigned int stride,
	unsigned int pad, const Diligent::BufferDesc &kind)
{
	if (zx::hwrender::ComputeBufferFits(buf != nullptr, capacity, count)) return true;
	capacity = zx::hwrender::ComputeGrownCapacity(count, pad);
	Diligent::BufferDesc bd = kind;
	bd.Size = (Diligent::Uint64)capacity * stride;
	buf.Release();
	CreateDeviceBuffer(bd, nullptr, &buf);
	if (!buf) { capacity = 0; return false; }
	return true;
}

// [rc4l] Uploads count vertices of stride bytes into a grow-only vertex buffer, re-created at ComputeGrownCapacity when short; false if none can be had.
//
// USAGE_DEFAULT + UpdateBuffer rather than a mapped dynamic buffer: this data changes once per
// frame, so it does not belong in the dynamic heap at all.
bool UploadGrowingVB(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const void *data, unsigned int count, unsigned int stride, unsigned int pad, const char *name, zx::hwrender::UploadShadow *shadow)
{
	Diligent::BufferDesc kind;
	kind.Name = name;
	kind.Usage = Diligent::USAGE_DEFAULT;
	kind.BindFlags = Diligent::BIND_VERTEX_BUFFER;
	if (!EnsureGrowingBuffer(vb, capacity, count, stride, pad, kind)) return false;
	// [rc4l] In pages: nuts.wad's 57,000 sprites wrote 8 MB a frame in one piece, and a browser kept 96 MB of upload pages.
	if (shadow != nullptr) UpdateBufferIfChanged(ctx, vb, *shadow, (size_t)count * stride, data);
	else UpdateBufferInPages(ctx, vb, 0, (size_t)count * stride, data);
	return true;
}

// [rc4l] Same vertex layout as FFlatVertex (3 float position, 2 float uv), so the level mesh's data
// is uploaded byte-for-byte with no conversion pass.
#include "features/hwrender/diligent/dgscene_glsl.h"   // [rc4l] the world pass's shaders

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

// [rc4l] The camera the engine's view globals describe now, as the backend frames it.
zx::hwrender::BackendCamera CurrentCamera()
{
	zx::hwrender::BackendCamera c;
	// [MobiusU9B] The view's own lens, as GL's SetProjection takes it, so a forced aspect ratio is stretched across the window and the walks' frustum covers the picture.
	const zx::viewsetup::ViewSetup &view = zx::viewsetup::CurrentView();
	zx::hwrender::ComputeCameraLens(g_frame.fovOverride, g_frame.aspectOverride, view.fov, view.ratio, view.fovRatio, c);
	zx::viewsetup::ViewCameraFromEngine(c);
	return c;
}
// [rc4l] The view window GL's SetViewport draws the player's view into, as a clip-space window over the whole target.
static zx::hwrender::NdcWindow ScreenViewWindow()
{
	const int sw = screen->GetWidth(), sh = screen->GetHeight();
	return zx::hwrender::ComputeNdcWindow(zx::hwrender::ComputeViewWindowViewport(sh, screenblocks, viewwindowx, viewwindowy, viewwidth, viewheight), sw, sh);
}

void BuildMVP(float *m)
{
	// [rc4l] The stereo eye being drawn, if any: moved across the view, its frustum leaning toward the middle.
	zx::hwrender::BackendCamera c = CurrentCamera();
	float eyeNdc = 0.f;
	ShiftCameraForEye(c, eyeNdc);
	zx::hwrender::ComputeBackendMVP(c, m);
	if (eyeNdc != 0.f) zx::hwrender::ComputeShiftNdcX(eyeNdc, m);
	// [rc4l] A view on the screen sits in the view window (a status bar, a smaller screenblocks); a camera texture fills its own target.
	if (g_frame.aspectOverride <= 0.f && screen != NULL) zx::hwrender::ComputeApplyNdcWindow(ScreenViewWindow(), m);
}
static void PrepareFrame(Diligent::IDeviceContext *ctx);

// [rc4l] Everything that draws the WORLD, with the render target already bound and cleared.
//
// Split out of DrawSceneOnce so the same code can draw into something other than the screen. A camera
// texture is the same world from a different viewpoint, so it wants exactly this and none of the
// screen-only parts -- no 2D layer, no present, no window pump. Portals and mirrors will want the
// same seam.


// [rc4l] The decal pass, in dgdecals.cpp. Runs between the world and the sprites.
void DrawDeferredDecals(Diligent::IDeviceContext *ctx);
void DrawHorizonPlanes(Diligent::IDeviceContext *ctx);

// [rc4l] Keep the readable depth buffer matched to the swapchain, recreating it when the size
// changes. Returns NULL if it cannot be made, and every caller then falls back to the swapchain's
// own depth buffer -- a missing screen-space effect is a better failure than a black screen.
Diligent::ITextureView *EnsureSceneDepth()
{
	auto *swap = GetSwapChain();
	auto *dev = GetDevice();
	if (!swap || !dev) return NULL;
	const auto &sd = swap->GetDesc();
	const int fw = FrameWidth(), fh = FrameHeight();
	if (!ComputeSceneTargetsRebuild(g_sceneDepth.RawPtr() != nullptr, g_sceneDepthW, g_sceneDepthH, fw, fh))
		return g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	if (fua_dg_verbose)
		Printf("Diligent scene targets rebuilt at %dx%d (were %dx%d)\n", fw, fh, g_sceneDepthW, g_sceneDepthH);

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
	td.Width = fw; td.Height = fh;
	td.MipLevels = 1;
	td.Format = sd.DepthBufferFormat;
	td.BindFlags = Diligent::BIND_DEPTH_STENCIL | Diligent::BIND_SHADER_RESOURCE;
	CreateDeviceTexture(td, nullptr, &g_sceneDepth);
	if (!g_sceneDepth) { g_sceneDepthW = g_sceneDepthH = 0; return NULL; }
	// The G-buffer shares the depth's lifetime and size: they are read together, one fragment at
	// a time, and a mismatch between them would be a reconstruction against the wrong surface.
	CreateNormalTarget(fw, fh, "fua scene", g_sceneNormal);
	g_sceneDepthW = fw; g_sceneDepthH = fh;
	return g_sceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
}

bool CreateNormalTarget(int w, int h, const char *name, Diligent::RefCntAutoPtr<Diligent::ITexture> &normal)
{
	if (!GetDevice() || w <= 0 || h <= 0) return false;
	FString normalName;
	normalName.Format("%s normal", name);
	Diligent::TextureDesc nd;
	nd.Name = normalName.GetChars();
	nd.Type = Diligent::RESOURCE_DIM_TEX_2D;
	nd.Width = (Diligent::Uint32)w; nd.Height = (Diligent::Uint32)h;
	nd.MipLevels = 1;
	nd.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
	nd.BindFlags = Diligent::BIND_RENDER_TARGET | Diligent::BIND_SHADER_RESOURCE;
	normal.Release();
	CreateDeviceTexture(nd, nullptr, &normal);
	return normal.RawPtr() != nullptr;
}

void ApplyPassViewport(Diligent::IDeviceContext *ctx)
{
	if (g_pass.targets.color == nullptr || g_pass.targets.w <= 0 || g_pass.targets.h <= 0) return;
	const Diligent::TextureDesc &td = g_pass.targets.color->GetTexture()->GetDesc();
	Diligent::Viewport vp;
	vp.TopLeftX = 0.f; vp.TopLeftY = 0.f;
	vp.Width = (float)g_pass.targets.w; vp.Height = (float)g_pass.targets.h;
	vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
	ctx->SetViewports(1, &vp, td.Width, td.Height);
}

void BeginPassTargets(Diligent::IDeviceContext *ctx, const PassTargets &targets, const float clear[4])
{
	g_pass.targets = targets;
	g_pass.gbufBound = targets.normal != nullptr;
	Diligent::ITextureView *bound[2] = { targets.color, targets.normal };
	ctx->SetRenderTargets(g_pass.gbufBound ? 2 : 1, bound, targets.depth, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ApplyPassViewport(ctx);
	ctx->ClearRenderTarget(targets.color, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	if (g_pass.gbufBound)
	{
		// Zero means "nothing drew here", which decodes to a zero-length normal -- and the decal pass
		// treats that as a surface it knows nothing about rather than one facing a particular way.
		const float clearN[4] = { 0.5f, 0.5f, 0.5f, 0.0f };
		ctx->ClearRenderTarget(targets.normal, clearN, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
	ctx->ClearDepthStencil(targets.depth, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

PassTargets ViewPassTargets(Diligent::ITexture *color, Diligent::ITexture *depth, Diligent::ITexture *normal, bool screenView)
{
	PassTargets t;
	t.color = color->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	t.depth = depth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	const int texW = (int)color->GetDesc().Width, texH = (int)color->GetDesc().Height;
	t.w = texW;
	t.h = texH;
	// [rc4l] A view of the screen draws where the player's view does, the scene's pixels at its top left, so a picture of it lands pixel for pixel.
	if (screenView && texW == FrameWidth() && texH == FrameHeight()) { t.w = SceneWidth(); t.h = SceneHeight(); }
	// [rc4l] A normal target of another size would be read at the wrong texels, so the view draws no marks instead.
	if (normal != NULL && (int)normal->GetDesc().Width == texW && (int)normal->GetDesc().Height == texH)
	{
		t.normal = normal->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
		t.normalRead = normal->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
		t.depthRead = depth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE);
	}
	return t;
}

int PassWidth() { return g_pass.targets.w > 0 ? g_pass.targets.w : FrameWidth(); }
int PassHeight() { return g_pass.targets.h > 0 ? g_pass.targets.h : FrameHeight(); }
Diligent::ITextureView *PassColorRTV() { return g_pass.targets.color; }
Diligent::ITextureView *PassDepthDSV() { return g_pass.targets.depth; }
Diligent::ITextureView *PassDepthSRV() { return g_pass.targets.depthRead; }
Diligent::ITextureView *PassNormalSRV() { return g_pass.targets.normalRead; }

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

void DrawSceneOnce(bool present, bool pump)
{
	ZX_ZONE("draw/scene");   // [rc4l] the backend frame, every pass
	const zx::framephase::Scope backendPhase(zx::framephase::kPhaseBackend);
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
	// [rc4l] The pipelines follow the texture filter, anisotropy, cull mode and material array size as they change in play, not only at the next level's upload.
	{
		FString err;
		if (!EnsureScenePipeline(err)) return;
		// [rc4l] And the decal pass's with them, made again here when a setting released both, not by the next mark.
		EnsureDecalPipelines();
	}
	// [rc4l] Textures the engine flushed (hqresize, hires, the texture format) are made again from the new pixels, and all of them when the filter starts or stops reading mips.
	{
		static unsigned flushedSeen = zx::materials::TexturesFlushed();
		static bool madeWithMips = MaterialsWithMips();
		if (flushedSeen != zx::materials::TexturesFlushed() || madeWithMips != MaterialsWithMips())
		{
			flushedSeen = zx::materials::TexturesFlushed();
			madeWithMips = MaterialsWithMips();
			FlushTextureCache();
		}
	}

	CollectGpuPasses();
	BeginGpuPass(ctx, kGpuFrame);
	// The sky portal is a whole second view of the map, so it is rendered here, beside the frame
	// rather than inside it -- DrawWorld cannot be re-entered while it is deriving this frame.
	// [rc4l] Before any pass: a height transfer's planes follow the eye across its surface, and a re-bake must land before the geometry is refreshed.
	zx::levelmesh::RefreshTransferArea();
	UploadWalkVisibility(ctx);
	g_frame.serial++;
	PrepareFrame(ctx);
	// [rc4l] GL's stereo 3D (vr_mode): each eye draws the world from its own place, an anaglyph keeping its first eye's picture to blend under the 2D.
	const zx::hwrender::StereoMode stereo = CurrentStereoMode();
	// [rc4l] features/dynres: the scale every screen-sized pass of this frame draws at, full while two eyes share the frame.
	BeginSceneFrame(stereo.eyes <= 1, BeginPostProcessFrame(stereo.eyes > 1));
	for (int eye = 0; eye < stereo.eyes; eye++)
	{
		SetStereoEye(stereo.shift[eye]);
		BuildMVP(g_mvp);
		RenderSkyPortal(ctx);
		RenderSkyLooks(ctx);
		RenderStackPortals(ctx);
		RenderPlaneMirrors(ctx);
		RenderMirrors(ctx);

		// [rc4l] Colour and the G-buffer together, for the opaque world only. DrawWorld drops back to
		// colour alone before the decals, which read the G-buffer rather than write it.
		PassTargets screen;
		screen.color = SceneRTV();
		screen.depth = EnsureSceneDepth();
		screen.w = SceneWidth(); screen.h = SceneHeight();
		if (screen.depth)
		{
			screen.normal = SceneNormalRTV();
			screen.depthRead = SceneDepthSRV();
			screen.normalRead = screen.normal ? SceneNormalSRV() : NULL;
		}
		else screen.depth = FrameDSV();
		const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
		BeginPassTargets(ctx, screen, clear);

		g_pass.mainView = true;
		if (eye == 0) BeginGpuPass(ctx, kGpuWorld);
		DrawWorld(ctx);
		if (eye == 0) EndGpuPass(ctx, kGpuWorld);
		g_pass.mainView = false;
		if (stereo.eyes > 1) KeepEyePicture(ctx, eye);
	}
	SetStereoEye(0.f);
	BuildMVP(g_mvp);
	if (stereo.eyes > 1) BlendEyes(ctx, stereo);
	ShowStackView(ctx);
	DrawHitboxes(ctx);
	DrawGpuLoad(ctx);
	// [Doruk-Aksoy] features/postprocess: its scene stages write the frame themselves, the weapon under them, else the scene is drawn up as before.
	int first2D = 0;
	if (!RunScenePostProcess(ctx, zx::draw2d::ViewQuads(), present, first2D)) UpscaleScene(ctx);

	// [rc4l] 2D last, over everything, with depth off entirely -- it is the frame's top layer.
	BeginGpuPass(ctx, kGpu2D);
	Draw2D(ctx, first2D);
	EndGpuPass(ctx, kGpu2D);
	RunScreenPostProcess(ctx);
	EndGpuPass(ctx, kGpuFrame);

	if (present)
	{
		PostProcessPresented();
		PresentFrame(ctx, swap, pump);
	}
}

bool SceneDrawable() { return g_scene.vb && g_pso.world[kWpOpaque] && g_scene.verts > 0; }

// [rc4l] Frames the CPU may queue ahead of the GPU; 0 lets it run free.
CVAR(Int, fua_dg_frames_ahead, 2, CVAR_ARCHIVE)

namespace {

Diligent::RefCntAutoPtr<Diligent::IFence> g_frameFence;
Diligent::Uint64 g_frameSignalled = 0;

}

// [rc4l] Each frame signals the fence with its number, and one too far ahead waits for the GPU to finish the frame that many back: unpaced, a frame on wdiex02 queued until Diligent's dynamic heap ran out and the frame died.
static void SignalFrame(Diligent::IDeviceContext *ctx)
{
	if (!g_frameFence)
	{
		Diligent::FenceDesc fd;
		fd.Name = "fua frame fence";
		fd.Type = Diligent::FENCE_TYPE_CPU_WAIT_ONLY;
		GetDevice()->CreateFence(fd, &g_frameFence);
		if (!g_frameFence) return;
	}
	ctx->EnqueueSignal(g_frameFence, ++g_frameSignalled);
}

static void PaceFrames()
{
	// [rc4l] A stepped loop is paced by its driver (a browser's animation frame), where the CPU cannot wait on the GPU at all.
	if (!g_frameFence || zx::mainloop::Stepped()) return;
	const Diligent::Uint64 wait = zx::hwrender::ComputeFenceWaitValue(g_frameSignalled, fua_dg_frames_ahead);
	if (wait != 0) g_frameFence->Wait(wait);
}

void ReleaseFrameFence()
{
	g_frameFence.Release();
	g_frameSignalled = 0;
}

Diligent::Uint64 FrameBeingBuilt() { return g_frameSignalled + 1; }
Diligent::Uint64 FrameFinishedOnGpu() { return g_frameFence ? g_frameFence->GetCompletedValue() : 0; }

// [rc4l] features/wipe's trace of what is about to be presented, read back whole, so only while fua_wipe_trace is on.
static void TraceWipeFrame()
{
	int w = 0, h = 0, format = 0;
	bool bgra = false;
	FString report;
	unsigned char *rgb = ReadBackBuffer(w, h, format, bgra, report);
	if (rgb == NULL) return;
	zx::wipe::TracePresented(rgb, w, h);
	delete[] rgb;
}

void EndUnpresentedFrame(Diligent::IDeviceContext *ctx)
{
	ctx->Flush();
	ctx->FinishFrame();
	SignalFrame(ctx);
	PaceFrames();
}

void PresentFrame(Diligent::IDeviceContext *ctx, Diligent::ISwapChain *swap, bool pump)
{
#ifdef ZX_ENABLE_REPLAY
	// [rc4l] Recorded before Present, which flips to an image whose contents are undefined.
	CaptureReplayFrame(ctx, swap);
#else
	(void)ctx;
#endif
	if (zx::wipe::TraceWanted()) TraceWipeFrame();
	g_stats.lastFramePasses = g_framePasses; g_stats.lastFrameWorldMs = g_frameWorldMs;
	g_stats.lastFramePassList = g_framePassList;
	for (int k = 0; k < 6; k++) { g_stats.lastFramePhase[k] = g_framePhase[k]; g_framePhase[k] = 0.0; }
	g_framePasses = 0; g_frameWorldMs = 0.0;
	g_framePassList = "";
	FinishFrame(ctx);
	SignalFrame(ctx);
	{
		const zx::framephase::Scope presentPhase(zx::framephase::kPhasePresent);
		zx::framelimit::WaitBeforePresent();
		swap->Present(zx::hwrender::ComputePresentInterval(vid_vsync, PagePaced()));
		PaceFrames();
	}
	FreeReleasedObjects(swap);
	RecordPresentInterval();
	PollLaterReadbacks();
	// [rc4l] The message pump costs more than the draw at these scales, so a benchmark pumps sparsely and a live frame once.
	if (pump) Fua_PumpBackendWindow(GetBackendWindow());
	// [rc4l] The embedded surface follows the engine's client area, which can change at runtime.
	Fua_SyncBackendWindowToParent(GetBackendWindow());
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
	CreateDeviceBuffer(bd, data != NULL ? &init : nullptr, &out);
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

// [rc4l] The jobs every pass shares, once a frame: moved geometry, the light list, the bindless table, animated materials and the sprite stream, sorted from the player's eye rather than the first pass's.
static void PrepareFrame(Diligent::IDeviceContext *ctx)
{
	if (fua_dg_frameprep && g_preparedSerial == g_frame.serial) return;
	g_preparedSerial = g_frame.serial;
	g_phase.Reset();
	{
		const zx::framephase::Scope uploadPhase(zx::framephase::kPhaseUpload);
		g_phase.geometry.Clock();  RefreshMovedGeometry(ctx);   g_phase.geometry.Unclock();
	}
	{
		const zx::framephase::Scope lightsPhase(zx::framephase::kPhaseLights);
		g_phase.lights.Clock();    CollectDynamicLights(ctx);   g_phase.lights.Unclock();
	}
	EnsureDynamicRecords();
	UpdateSectorLightTable(ctx);
	// [rc4l] Before anything is recorded: rebuilding a binding that a command buffer already holds is a use-after-free, and this only works on the frames the material table changed.
	if (fua_dg_bindless && MaterialSlotCapacity() > 0) { MeasureSlotWorkingSet(); ReserveDynamicSlots(); RefreshBindless(); }
	g_phase.animation.Clock();
	// [rc4l] Re-resolve animated textures, since a batch's material was resolved once at bake time and would freeze on its first frame.
	// [rc4l] Each base texture asked once, its batches gathered by ComputeGroupByKey: wdiex01's 48,000 batches share about 700 textures, and asking per batch cost 0.06 ms a frame.
	if (fua_dg_animate)
	{
		static std::vector<int> order;
		static std::vector<::hwrender::BatchRun> groups;
		static std::vector<const void *> showing;
		static unsigned int groupedFor = ~0u;
		if (groupedFor != g_scene.batchesGen)
		{
			groupedFor = g_scene.batchesGen;
			static std::vector<uint64_t> keys;
			keys.resize(g_scene.batches.Size());
			for (unsigned i = 0; i < g_scene.batches.Size(); i++) keys[i] = (uint64_t)(uintptr_t)g_scene.batches[i].baseTex;
			::hwrender::ComputeGroupByKey(keys.empty() ? NULL : &keys[0], (int)keys.size(), order, groups);
			showing.assign(groups.size(), NULL);
		}
		for (size_t g = 0; g < groups.size(); g++)
		{
			const void *base = g_scene.batches[order[groups[g].start]].baseTex;
			if (base == NULL) continue;
			const void *now = zx::materials::MaterialShowing(base);
			if (now == NULL || now == showing[g]) continue;
			showing[g] = now;
			// [rc4l] One binding per pipeline the group's batches use, since they all show the same frame now.
			Diligent::IPipelineState *psoAsked = NULL;
			Diligent::IShaderResourceBinding *srbAsked = NULL;
			for (int k = groups[g].start; k < groups[g].start + groups[g].count; k++)
			{
				SceneBatch &b = g_scene.batches[order[k]];
				if (now == b.resolved) continue;
				b.resolved = now;
				g_stats.animSwaps++;
				Diligent::IPipelineState *pso = BatchPSO(b.blend);
				if (pso != psoAsked) { psoAsked = pso; srbAsked = GetMaterialSRB(pso, now); }
				if (srbAsked) b.srb = srbAsked;
			}
		}
	}
	BuildDynamic(ctx);
	g_phase.animation.Unclock();
	g_framePhase[0] += g_phase.geometry.TimeMS();
	g_framePhase[1] += g_phase.lights.TimeMS();
	g_framePhase[4] += g_phase.animation.TimeMS();
}

// [rc4l] The main view's targets: the frame's colour and the scene's depth, with the G-buffer's normal beside them when `normal`.
// [rc4l] The pass's own colour and depth, with its normals when asked.
static void BindWorldTargets(Diligent::IDeviceContext *ctx, bool normal)
{
	Diligent::ITextureView *targets[2] = { g_pass.targets.color, g_pass.targets.normal };
	ctx->SetRenderTargets(normal && g_pass.targets.normal ? 2 : 1, targets, g_pass.targets.depth, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ApplyPassViewport(ctx);
}

void DrawWorld(Diligent::IDeviceContext *ctx)
{
	ZX_ZONE("draw/world");   // [rc4l] the world pass, once per view it draws
	cycle_t whole;
	whole.Reset();
	whole.Clock();
	struct PassTimer
	{
		cycle_t &c;
		~PassTimer()
		{
			c.Unclock();
			g_framePasses++; g_frameWorldMs += c.TimeMS();
			const double ph[6] = { g_phase.geometry.TimeMS(), g_phase.lights.TimeMS(), g_phase.clusters.TimeMS(),
				g_phase.constants.TimeMS(), g_phase.animation.TimeMS(), g_phase.draw.TimeMS() };
			for (int k = 0; k < 6; k++) g_framePhase[k] += ph[k];
			g_framePassList.AppendFormat(" %s %.2f", zx::hwrender::ComputeWorldPassName(g_pass.mainView, g_pass.lineMirror,
				g_pass.stackSlot, g_pass.inCameraPass, g_pass.inSkyPortal), c.TimeMS());
		}
	} passTimer = { whole };
	// [rc4l] Lights are collected BEFORE the constants are written -- the shader reads the count from
	// there, so collecting afterwards would light every frame with the previous frame's count.
	PrepareFrame(ctx);
	g_phase.Reset();
	g_stats.batchesDrawn = g_stats.worldDraws = 0;
	// [rc4l] Per pass, because the clusters are binned against this pass's camera.
	g_phase.clusters.Clock();  BuildLightClusters(ctx);     g_phase.clusters.Unclock();
	g_phase.constants.Clock();

	{
		Diligent::MapHelper<float> cb(ctx, g_cb, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		WriteSceneConstants(&cb[0], g_pass.clipPlane, (float)PassWidth(), (float)PassHeight(),
			BindlessActive() ? (float)MaterialSlotCount() : 0.f);
	}

	g_phase.constants.Unclock();

	g_phase.draw.Clock();

	// [rc4l] Sky first, with depth off, so the world paints over whatever it does not cover.
	if (fua_dg_sky)
	{
		// [rc4l] Under colour and depth alone, the G-buffer bound again after, since the sky's pipelines declare one colour target and WebGPU holds a pipeline to its pass's.
		const bool gbuf = g_pass.gbufBound;
		if (gbuf) BindWorldTargets(ctx, false);
		DrawSkyBackground(ctx);
		if (gbuf) BindWorldTargets(ctx, true);
	}

	Diligent::IBuffer *vbs[] = { g_scene.vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_pso.world[kWpOpaque]);

	// [rc4l] One alpha-tested pipeline for everything.
	//
	// An opaque/masked split was tried and REVERTED: it neither helped (presented stayed 1.77 ms,
	// because that time is swapchain present, not fragment shading) nor rendered correctly -- routing
	// materials FMaterial::isMasked() calls opaque made their transparent texels paint over detail
	// behind them. The lesson is that isMasked() answers a GL-pipeline question, not "does this
	// texture have see-through pixels", so it cannot drive a pass split on its own.
	const int maskedPipeline = g_pass.gbufBound ? kWpMaskedGBuffer : kWpMasked;
	Diligent::IPipelineState *boundRun = g_pso.world[maskedPipeline];
	ctx->SetPipelineState(boundRun);

	// [rc4l] g_frame.drawRepeat re-draws the whole scene N times per frame.
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
	for (int rep = 0; rep < g_frame.drawRepeat; rep++)
	{
		Diligent::IShaderResourceBinding *bound = NULL;
		unsigned runFirst = 0, runCount = 0;
		for (unsigned bi = 0; bi <= g_scene.batches.Size(); bi++)
		{
			SceneBatch *b = (bi < g_scene.batches.Size()) ? &g_scene.batches[bi] : NULL;
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
			// [rc4l] Only when GL made no skybox walk this frame: with one, the scene shader drops what it did not reach, as a stacked view's pass does.
			if (take && g_pass.inSkyPortal && !zx::levelmesh::SkyboxWalked() && fua_dg_skyportal_reach > 0.f &&
			    ComputeBeyondSkyPortalReach(b->minX, b->maxX, b->minZ, b->maxZ,
			                                g_pass.skyCamX, g_pass.skyCamY, fua_dg_skyportal_reach))
				take = false;

			Diligent::IShaderResourceBinding *srb = NULL;
			Diligent::IPipelineState *run = NULL;
			if (take)
			{
				// [rc4l] One binding for the whole pass when the array is carrying the materials. This
				// is the cost the culling experiment could not remove: 166 batches were 0.445 ms of
				// submit on Sunder MAP16, almost all of it committing a descriptor set per batch.
				srb = WorldSRB(g_pso.world[maskedPipeline].RawPtr());
				// [Doruk-Aksoy] A batch with a GLDEFS shader draws with that shader's program, under the same binding.
				run = ScenePSO(maskedPipeline, b->userShader);
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
			// [Doruk-Aksoy] ...and the same program.
			const bool extends = fua_dg_mergedraws && take && runCount != 0 && srb == bound && run == boundRun &&
				b->first == runFirst + runCount;
			if (extends) { runCount += b->count; g_stats.batchesDrawn++; continue; }

			if (runCount != 0)
			{
				Diligent::DrawAttribs draw;
				draw.NumVertices = runCount;
				draw.StartVertexLocation = runFirst;
				draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
				ctx->Draw(draw);
				g_stats.worldDraws++;
				runCount = 0;
			}
			// [rc4l] A batch skipped draws nothing and binds nothing, so the binding stays committed across it; the sky pass skips most of the level.
			if (!take) { if (!fua_dg_loopcommits) bound = NULL; continue; }
			if (run != boundRun) { ctx->SetPipelineState(run); boundRun = run; bound = NULL; }
			if (srb != bound)
			{
				ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
				bound = srb;
			}
			runFirst = b->first; runCount = b->count;
			g_stats.batchesDrawn++;
		}
	}

	// [rc4l] The G-buffer is finished with: everything from here reads it or ignores it, and every
	// pipeline after this point declares a single attachment.
	if (g_pass.gbufBound)
	{
		BindWorldTargets(ctx, false);
		g_pass.gbufBound = false;
	}

	// [rc4l] Decals mark surfaces, so they go with the surfaces -- after the world and before the
	// sprites. Anything standing in front of a mark is then drawn over it because it is in a LATER
	// pass, which is a fact about the frame rather than something a sort has to rediscover.
	// [rc4l] The floor and ceiling marks features/decals made, whenever there are any.
	if (zx::levelmesh::ProjectedDecalsActive()) DrawDeferredDecals(ctx);

	// [rc4l] Sprites were built in PrepareFrame and draw in the sorted pass below, except the solid ones, which belong with the world: a model has to occlude itself.
	DrawSolidDyn(ctx);

	// [rc4l] Horizon lines, painted with the planes they open onto -- see features/horizon; built for the player's eye, so a stacked view takes none.
	if (g_pass.stackSlot < 0) DrawHorizonPlanes(ctx);

	// [rc4l] ...and the sky as geometry, before the blend pass because it is opaque.
	DrawSkySurfaces(ctx);
	DrawStackSurfaces(ctx);

	DrawPlaneMirrorSurfaces(ctx);
	DrawLineMirrorSurfaces(ctx);

	// [rc4l] Then everything that blends -- translucent 3D floors, translucent middle textures and
	// translucent sprites -- in ONE back-to-front pass, so the world and the sprites sort against each
	// other rather than being layered by which pass came last.
	DrawBlended(ctx);
	g_phase.draw.Unclock();
}

}} // namespace zx::hwrender
#endif // FUA_DILIGENT
