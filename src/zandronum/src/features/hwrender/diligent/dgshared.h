// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The handful of things a second pass needs from the scene renderer.
//
// dgscene.cpp owns the device, the swapchain, the scene's constant buffer, the view matrix and the
// depth and normal targets. A pass that runs inside the same frame -- the decals do -- needs to read
// all of them, and the alternative to naming them here was for it to live inside dgscene.cpp, which
// is how that file got to four thousand lines.
//
// Deliberately small: everything on this list is READ by another pass, never written. Anything that
// wants to change the scene's state belongs in dgscene.cpp with the rest of it.

#ifndef ZX_DGSHARED_H
#define ZX_DGSHARED_H

// [rc4l] The scene constant block every pass binds, declared once; uFrame.x is the warp clock in seconds.
#define FUA_CONSTANTS_GLSL \
	"layout(binding = 0) uniform Constants { mat4 uMVP; vec4 uCameraPos; vec4 uLightParams; vec4 uClipPlane; vec4 uScreen; vec4 uSkyColor; vec4 uFrame; vec4 uFixedColormap; vec4 uFixedRange; vec4 uOptions; vec4 uShadeEnv; vec4 uShadeEnv2; vec4 uShadeFog; };\n"

// [rc4l] main.fp's fixed colormap on a lit texel, after a block declaring uFixedColormap and uFixedRange: mode 1 (w) maps its grey into the ramp, as invulnerability inverts, and mode 2 multiplies it, as light amplification and a torch tint.
#define FUA_FIXED_COLORMAP_GLSL \
	"vec3 fuaFixedColormap(vec3 c) {\n" \
	"    if (uFixedColormap.w < 1.5) return clamp(uFixedColormap.rgb + dot(c, vec3(0.3, 0.56, 0.14)) * uFixedRange.rgb, 0.0, 1.0);\n" \
	"    return c * uFixedColormap.rgb;\n" \
	"}\n"

// [rc4l] GL's fuzz shaders, line for line with fuzz_compute: fuaFuzzSplat is where the jagged kind (5) samples, fuaFuzz what a kind (1-7) leaves of the texel at tc, `timer` seconds in.
#define FUA_FUZZ_GLSL \
	"float fuaFuzzFlicker(float texX, float texY, float timer) {\n" \
	"    return mod(timer * 2.0 + ((texX / texY) * 21.0 + (texY / texX) * 13.0), 0.5);\n" \
	"}\n" \
	"vec2 fuaFuzzSplat(vec2 tc, float timer) {\n" \
	"    const float pi = 3.14159265358979323846;\n" \
	"    return vec2(tc.x + mod(sin(pi * 2.0 * (tc.y + timer * 2.0)), 0.1) * 0.1, tc.y + mod(cos(pi * 2.0 * (tc.x + timer * 2.0)), 0.1) * 0.1);\n" \
	"}\n" \
	"vec4 fuaFuzz(int kind, vec2 tc, float timer, vec4 texel) {\n" \
	"    float test = 1.0;\n" \
	"    if (kind == 1 || kind == 2) {\n" \
	"        vec2 c = kind == 1 ? vec2(ivec2(tc * 128.0)) / 128.0 : tc;\n" \
	"        test = fuaFuzzFlicker(c.x / 3.0 + 0.66, 0.34 - c.y / 3.0, timer);\n" \
	"    } else if (kind >= 3 && kind <= 5) {\n" \
	"        test = fuaFuzzFlicker(sin(tc.x * 100.0 + timer * 5.0), cos(tc.x * 100.0 + timer * 5.0), timer);\n" \
	"    } else if (kind == 6 || kind == 7) {\n" \
	"        vec2 c = kind == 6 ? vec2(ivec2(tc * 128.0)) / 128.0 : tc;\n" \
	"        test = fuaFuzzFlicker(sin(mod(c.x * 100.0 + timer * 5.0, 3.489)) + c.x / 4.0, cos(mod(c.y * 100.0 + timer * 5.0, 3.489)) + c.y / 4.0, timer);\n" \
	"    } else return texel;\n" \
	"    texel.a *= test;\n" \
	"    if (kind != 4 && kind != 5) texel.rgb = vec3(0.0);\n" \
	"    return texel;\n" \
	"}\n"

// [rc4l] main.fp's damage tint, line for line with ComputeTintTexel: `tint` the colour and strength, `range` the texture-V span (none at x, full at y) and z 1 for a glow.
#define FUA_DAMAGE_TINT_GLSL \
	"vec4 fuaDamageTint(vec4 t, float v, vec4 tint, vec3 range) {\n" \
	"    if (tint.a <= 0.0) return t;\n" \
	"    float f = clamp((v - range.x) / max(range.y - range.x, 0.0001), 0.0, 1.0);\n" \
	"    vec3 tinted = range.z > 0.5 ? 1.0 - (1.0 - t.rgb) * (1.0 - tint.rgb) : t.rgb * tint.rgb;\n" \
	"    return vec4(mix(t.rgb, tinted, f * tint.a), t.a);\n" \
	"}\n"

// [rc4l] A line mirror's pass drops every fragment at world position p behind the glass; uClipPlane.w of 0 means no mirror.
#define FUA_MIRROR_CLIP_GLSL(p) \
	"    if (uClipPlane.w != 0.0 && dot(" p ", uClipPlane.xyz) + uClipPlane.w < 0.0) discard;\n"

// [rc4l] One piece's record as every shader reads it, declared once so its stride cannot drift from ScenePieceData's.
#define FUA_PIECEDATA_GLSL \
	"struct PieceData { vec4 colorLight; vec4 fogAlphaMat; vec4 fogColor; vec4 normal; vec4 floodU; vec4 floodV; vec4 actorLight; vec4 bright; vec4 glowTop; vec4 glowBottom; vec4 glowTopPlane; vec4 glowBottomPlane; vec4 tint; vec4 tintRange; vec4 extra; };\n" \
	"layout(std430, binding = 9) readonly buffer Pieces { PieceData pieces[]; } PiecesIn;\n"

// [rc4l] A level surface lit from the sector light table, after FUA_CONSTANTS_GLSL and FUA_PIECEDATA_GLSL: fuaSectorLit gives a record kTexFlagSectorLit marks the shading ComputeSectorLitShade gives it, ComputeSectorLitInput then ComputeShading line for line, and leaves any other record as it is.
#define FUA_SECTORLIT_GLSL \
	"layout(std430, binding = 13) readonly buffer SectorLight { uint sectorLight[]; } SectorLightIn;\n" \
	/* [rc4l] ComputeLightLevel, its 1.95 the float the CPU multiplies by and precise so no fused multiply rounds it otherwise. */ \
	"int fuaShadeLightLevel(int lightLevel, int relLight, int mode) {\n" \
	"    if (lightLevel == 0) return 0;\n" \
	"    int light = lightLevel;\n" \
	"    if ((mode & 2) != 0 && lightLevel < 192) {\n" \
	"        precise float f = 192.0 - float(192 - lightLevel) * uintBitsToFloat(0x3ff9999au);\n" \
	"        light = int(roundEven(f));\n" \
	"    }\n" \
	"    int ambient = int(uShadeEnv.y);\n" \
	"    if (light < ambient && mode != 8) { light = ambient; if (relLight < 0) relLight >>= 1; }\n" \
	"    return clamp(light + relLight, 0, 255);\n" \
	"}\n" \
	/* [rc4l] ComputeLightColor, as bytes. */ \
	"ivec3 fuaShadeLightColor(int light, uint color, int blendFactor, int mode) {\n" \
	"    ivec3 c = ivec3(int((color >> 16) & 255u), int((color >> 8) & 255u), int(color & 255u));\n" \
	"    if (mode == 8) return c;\n" \
	"    ivec3 r = blendFactor == 0 ? c * light / 255 : (ivec3(light * (255 - blendFactor)) + c * blendFactor) / 255;\n" \
	"    return r & ivec3(255);\n" \
	"}\n" \
	/* [rc4l] ComputeDistFog. */ \
	"float fuaShadeDistFog(int distFog, bool softTable, int i) {\n" \
	"    if (!softTable) {\n" \
	"        if (i < 164) return float((distFog >> 1) + distFog * (164 - i) / 164);\n" \
	"        if (i < 230) return float((distFog >> 1) - (distFog >> 1) * (i - 164) / (230 - 164));\n" \
	"        return 0.0;\n" \
	"    }\n" \
	"    if (i < 128) return 6.0 + float(distFog >> 1) + float(distFog * (128 - i) / 48);\n" \
	"    if (i < 216) return (216.0 - float(i)) / (216.0 - 128.0) * float(distFog) / 10.0;\n" \
	"    return 0.0;\n" \
	"}\n" \
	/* [rc4l] ComputeFogDensity, the outside fog's colour a 24-bit whole number in uShadeFog.x and its alpha in .y. */ \
	"float fuaShadeFogDensity(int lightLevel, uint fogColor, int mode) {\n" \
	"    int levelDensity = int(uShadeEnv2.y);\n" \
	"    if ((mode & 4) != 0) return float(levelDensity != 0 ? levelDensity : 18);\n" \
	"    if ((fogColor & 0xffffffu) == 0u) return mode != 8 ? fuaShadeDistFog(int(uShadeEnv.w), mode != 0, clamp(lightLevel, 0, 255)) : 0.0;\n" \
	"    int outsideDensity = int(uShadeEnv2.z);\n" \
	"    if (outsideDensity != 0 && int(uShadeFog.y) != 255 && (fogColor & 0xffffffu) == uint(uShadeFog.x)) return float(outsideDensity);\n" \
	"    if (levelDensity != 0) return float(levelDensity);\n" \
	"    if (lightLevel < 248) return float(clamp(255 - lightLevel, 30, 255));\n" \
	"    return 0.0;\n" \
	"}\n" \
	"PieceData fuaSectorLit(PieceData pd) {\n" \
	"    if ((int(pd.extra.w) & 32768) == 0) return pd;\n" \
	"    uint b = uint(pd.colorLight.x) * 8u;\n" \
	"    int kind = int(pd.colorLight.y), sideLight = int(pd.colorLight.z), contrast = int(pd.colorLight.w);\n" \
	"    bool sideAbsolute = pd.fogColor.x > 0.5, sideLightFog = pd.fogColor.y > 0.5, noFog = pd.fogColor.z > 0.5;\n" \
	"    int flags = int(uShadeEnv2.x), mode = int(uShadeEnv.x), fogMode = int(uShadeEnv.z);\n" \
	"    bool fadeTable = (flags & 1) != 0, brightFog = (flags & 2) != 0;\n" \
	"    uint lightColor = SectorLightIn.sectorLight[b + 3u], fadeColor = SectorLightIn.sectorLight[b + 4u];\n" \
	"    int blendFactor = int(SectorLightIn.sectorLight[b + 5u]), desaturation = int(SectorLightIn.sectorLight[b + 6u]);\n" \
	/* [rc4l] ComputeSectorLitInput. */ \
	"    int lightLevel = 0, relLight = 0;\n" \
	"    if (kind == 0) lightLevel = int(SectorLightIn.sectorLight[b + 1u]);\n" \
	"    else if (kind == 1) lightLevel = int(SectorLightIn.sectorLight[b + 2u]);\n" \
	"    else {\n" \
	"        int org = clamp(int(SectorLightIn.sectorLight[b]), 0, 255);\n" \
	"        bool foggy = (fadeColor & 0xffffffu) != 0u || fadeTable;\n" \
	"        int base = sideAbsolute ? sideLight : org;\n" \
	"        int rel = foggy ? 0 : contrast;\n" \
	"        if (!sideAbsolute && (!foggy || sideLightFog)) base = clamp(base + sideLight, 0, 255);\n" \
	"        lightLevel = clamp(base, 0, 255);\n" \
	"        relLight = org >= 253 ? 0 : (lightLevel - rel > 256 ? 256 - lightLevel + rel : rel);\n" \
	"    }\n" \
	/* [rc4l] ComputeShading with the player's extra light, outside the fixed colormap and any skybox, as a level surface is baked. */ \
	"    relLight += int(uShadeEnv2.w);\n" \
	"    ivec3 c = fuaShadeLightColor(fuaShadeLightLevel(lightLevel, relLight, mode), lightColor, blendFactor, mode);\n" \
	"    float soft = mode == 8 ? float(clamp(lightLevel + relLight, 0, 255)) / 255.0 : -1.0;\n" \
	"    float density = 0.0;\n" \
	"    uint color = 0u;\n" \
	"    if (fadeTable) { density = 70.0; color = 0x808080u; }\n" \
	"    else if (!noFog) { density = fuaShadeFogDensity(lightLevel, fadeColor, mode); color = fadeColor & 0xffffffu; }\n" \
	"    float scale = uintBitsToFloat(0xb7bd18b6u);\n" \
	"    if (density == 0.0 || fogMode == 0) { color = 0u; pd.fogAlphaMat.x = 0.0 * scale; pd.fogAlphaMat.z = 0.0; }\n" \
	"    else {\n" \
	"        pd.fogAlphaMat.x = density * scale;\n" \
	"        pd.fogAlphaMat.z = float(color == 0u ? fogMode : -fogMode);\n" \
	"        if (mode == 8 && brightFog && color != 0u) soft = 1.0;\n" \
	"    }\n" \
	"    pd.colorLight = vec4(vec3(c) / 255.0, soft);\n" \
	"    pd.fogColor.xyz = vec3(float((color >> 16) & 255u), float((color >> 8) & 255u), float(color & 255u)) / 255.0;\n" \
	"    pd.extra.x = float(desaturation) / 255.0;\n" \
	"    return pd;\n" \
	"}\n"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "Buffer.h"
#include "TextureView.h"
#include "Sampler.h"
#include "PipelineState.h"
#include "ShaderResourceBinding.h"
#include "Shader.h"
#include "RefCntAutoPtr.hpp"
#include "features/hwrender/diligent/dgdevice.h"   // [rc4l] CreateDeviceTexture, CreateDeviceBuffer
#include "features/hwrender/computation/uploadguard_compute.h"   // [rc4l] UploadShadow

class FString;

namespace zx { namespace hwrender {

Diligent::IRenderDevice  *GetDevice();
Diligent::ISwapChain     *GetSwapChain();
// [rc4l] Where the frame is drawn (dgtarget.cpp): beside GL the window's back buffer, without GL an offscreen target at the screen's size, which FinishFrame draws up to the window before the present; its texture is what a screenshot, a replay or a wipe reads.
bool FrameOffscreen();
Diligent::ITextureView *FrameRTV();
Diligent::ITextureView *FrameDSV();
Diligent::ITexture *FrameTexture();
// [rc4l] Which making of the offscreen target this is, so a picture drawn into an earlier one is not taken for this one's.
unsigned int FrameGeneration();
int FrameWidth();
int FrameHeight();
void FinishFrame(Diligent::IDeviceContext *ctx);
void ReleaseFrameTarget();
// [rc4l] features/dynres: the frame's scene scale, asked once a frame before its first 3D pass; mayScale false holds full scale.
void BeginSceneFrame(bool mayScale);
// [rc4l] Whether this frame's 3D is drawn below full scale, into the top left SceneWidth by SceneHeight of every screen-sized target.
bool SceneScaled();
int SceneWidth();
int SceneHeight();
// [rc4l] Where the player's view draws: the scene target below full scale, else the frame itself.
Diligent::ITextureView *SceneRTV();
// [rc4l] The scene painted over the whole frame, before the 2D, when it was drawn below full scale.
void UpscaleScene(Diligent::IDeviceContext *ctx);
// [rc4l] fua_dg_gpuload's work over the scene's pixels (dggpuload.cpp), and its pipeline let go at shutdown.
void DrawGpuLoad(Diligent::IDeviceContext *ctx);
void ReleaseGpuLoad();
// [rc4l] The frame fence released at the backend's shutdown (dgwindow.cpp), while the console its destructor reports to is still there.
void ReleaseFrameFence();
// [rc4l] Everything a level owned on the GPU (dgframe.cpp), for a level change and for shutdown's report of what is still held.
void ReleaseLevelResources();
// [rc4l] The sprite atlas's pages and images dropped (dgatlas.cpp), which outlive a level and go only when the engine's pixels change or the backend stops.
void ResetAtlas();

// [rc4l] GL draws its views with GL_DEPTH_CLAMP, keeping a surface nearer than the near plane at the near depth instead of cutting it away; a pipeline drawing the world asks for the same when the device can.
void ClampDepthLikeGL(Diligent::GraphicsPipelineDesc &gp);
// [rc4l] A render target blending as kWpBlend* `blend` (ComputeBlendEquation), its alpha accumulated over as GL's is.
void SetBlendEquation(Diligent::RenderTargetBlendDesc &rt, int blend);

// [rc4l] One GLSL stage compiled as written, or null when it fails (Diligent says why through its message callback).
Diligent::RefCntAutoPtr<Diligent::IShader> CompileShader(Diligent::SHADER_TYPE type, const char *name, const char *source);
// [rc4l] Every backend pipeline is made here, its shaders' storage buffers marked as never bound with a dynamic offset (none is), since WebGPU allows a pipeline only 8 that are.
void CreatePipeline(const Diligent::GraphicsPipelineStateCreateInfo &pci, Diligent::IPipelineState **pso);

// The texture a material draws with, and a resource binding for one against a given pipeline. Both
// cache, so asking every frame is what they are for.
Diligent::ITextureView          *GetMaterialSRV(const void *materialPtr, int translation);
Diligent::IShaderResourceBinding *GetMaterialSRB(Diligent::IPipelineState *pso, const void *material,
                                                 int translation = 0);

// [rc4l] The G-buffer the world pass wrote: what is in front of each pixel, and which way it faces.
// Null before the first frame, or when the pass that fills them is disabled.
Diligent::ITextureView *SceneDepthSRV();
Diligent::ITextureView *SceneNormalSRV();
Diligent::ITextureView *SceneDepthDSV();
// [rc4l] The targets of the pass being drawn (BeginPassTargets): its colour and depth, the depth and normals its decal pass reads (null for a view without them), and its size, the frame's before any pass set one.
Diligent::ITextureView *PassColorRTV();
Diligent::ITextureView *PassDepthDSV();
// [rc4l] The pass's own viewport, its w by h at the top left of its targets, set again after anything binds them, since binding resets it to the whole target.
void ApplyPassViewport(Diligent::IDeviceContext *ctx);
Diligent::ITextureView *PassDepthSRV();
Diligent::ITextureView *PassNormalSRV();
int PassWidth();
int PassHeight();

// The scene's own constant buffer -- uMVP and friends -- and this frame's view-projection, which a
// pass needs in order to invert it and turn a depth sample back into a world position.
Diligent::IBuffer *SceneConstantsCB();
const float       *SceneMVP();

// [rc4l] A shader-readable structured buffer of count elements of stride bytes in out, filled from data when given; false when the device refuses it.
bool CreateStructuredBuffer(const char *name, unsigned stride, unsigned long long count, const void *data,
	Diligent::RefCntAutoPtr<Diligent::IBuffer> &out);

// [rc4l] Bind the scene constants on every stage of pso that declares them; false when its vertex stage does not.
bool BindSceneConstants(Diligent::IPipelineState *pso);


// The engine's texture filter, as a sampler. Shared so a second pass cannot quietly disagree with
// the world about how textures are filtered.
void FillSamplerFromEngine(Diligent::SamplerDesc &samp);

// [rc4l] Drop the horizon and decal passes' pipelines, so the next draw builds them with the engine's current filter.
void ReleaseHorizonPipeline();
void ReleaseDecalPipelines();

// [rc4l] While the sky portal pass is drawing: where its camera stands and how far it may reach.
bool SkyPortalReach(float &camX, float &camY, float &reach);

// [rc4l] The horizon windows' planes, drawn after the opaque world -- see features/horizon.
void DrawHorizonPlanes(Diligent::IDeviceContext *ctx);

// [rc4l] Writes bytes into buf at offset, the one place the backend sends a buffer anything, counted under gpu/upload_bytes and by buffer under gpu/upload/.
void SendToBuffer(Diligent::IDeviceContext *ctx, Diligent::IBuffer *buf, unsigned long long offset, size_t bytes, const void *data);
// [rc4l] Ends a frame for gpu/upload/'s bytes per frame.
void NoteUploadFrame();
// [rc4l] The sector light table brought up to the render copy's sectors, sending only the entries that changed, once a frame before anything draws.
void UpdateSectorLightTable(Diligent::IDeviceContext *ctx);
// [rc4l] Bytes written into buf at offset in pieces no bigger than an upload page, so a big write takes pooled pages rather than one its own size, which a browser's device keeps for good.
void UpdateBufferInPages(Diligent::IDeviceContext *ctx, Diligent::IBuffer *buf, unsigned long long offset, size_t bytes, const void *data);
// [rc4l] The deferred decal pass's pipelines and buffers made for the level now, as its scene's are, rather than by its first mark; false when they cannot be (fua_projdecals_stats says why).
bool EnsureDecalPipelines();

// [rc4l] While one lives, UpdateBufferInPages flushes a browser's context each time kUploadFlushBytes have gone, so its upload pages go back to the pool as it goes, for a level's upload and its rebuilds, which come before the frame maps or draws anything a flush would cut short.
struct UploadFlushing
{
	UploadFlushing();
	~UploadFlushing();
};
// [rc4l] UpdateBufferInPages, unless buf already holds those bytes from the last upload through shadow (uploadguard_compute), counting what it spared under gpu/upload_spared.
void UpdateBufferIfChanged(Diligent::IDeviceContext *ctx, Diligent::IBuffer *buf, zx::hwrender::UploadShadow &shadow, size_t bytes, const void *data);
// [rc4l] Counts bytes another upload sent, such as a texture's, toward the flush an UploadFlushing makes due.
void NoteUploadSent(Diligent::IDeviceContext *ctx, size_t bytes);
// [rc4l] The translation the floor mark pass asks a decal's graphic for (ComputeDecalPassTranslation), shared with the precache that makes it ahead.
int DecalPassTranslation(const void *material, int translation, bool redToAlpha);
// [rc4l] The textures the engine marked for this level (QueueMaterialPrecache) made now, as its scene is set up, rather than by the frames that first draw them (dgtexture.cpp).
void PrecacheQueuedMaterials();
// [rc4l] A grow-only buffer of at least count elements of stride bytes, made again from kind's name, usage, binding and access at ComputeGrownCapacity(count, pad) when short; false if none can be had.
bool EnsureGrowingBuffer(Diligent::RefCntAutoPtr<Diligent::IBuffer> &buf, unsigned int &capacity, unsigned int count, unsigned int stride,
	unsigned int pad, const Diligent::BufferDesc &kind);
// [rc4l] count vertices into a grow-only vertex buffer, unsent when shadow is given and they are what it already holds.
bool UploadGrowingVB(Diligent::IDeviceContext *ctx, Diligent::RefCntAutoPtr<Diligent::IBuffer> &vb,
	unsigned int &capacity, const void *data, unsigned int count, unsigned int stride, unsigned int pad, const char *name,
	zx::hwrender::UploadShadow *shadow = nullptr);

// [rc4l] The 2D pass (dg2d.cpp): the captured HUD, menus and console over the frame, and what it drew last.
void Draw2D(Diligent::IDeviceContext *ctx);
void Get2DStats(int &quads, int &draws);

// [rc4l] The light pass (dglights.cpp): its buffers, created once and bound statically to every world pipeline, the per-tic light list, the per-pass cluster grid and its counters.
bool EnsureLightBuffers(FString &err);
void BindLightBuffers(Diligent::IPipelineState *pso);
void CollectDynamicLights(Diligent::IDeviceContext *ctx);
void BuildLightClusters(Diligent::IDeviceContext *ctx);
int LightCount();
int ClusterCells();
void AppendLightStats(FString &report);

}} // namespace zx::hwrender

#endif // ZX_DGSHARED_H
