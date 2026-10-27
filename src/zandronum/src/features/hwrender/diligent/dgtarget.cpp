// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a frame is drawn: beside GL the window's own back buffer, and in a launch without GL an offscreen target at the screen's size, drawn up to the window at present as GL's scale buffer is, which keeps the last frame for a wipe.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/diligent/dggputimer.h"   // [rc4l] LastGpuPassMs
#include "features/hwrender/backendonly.h"   // [rc4l] BackendOnly
#include "features/hwrender/computation/texfilter_compute.h"
#include "features/dynres/dynres.h"
#include "features/dynres/computation/dynres_compute.h"
#include "features/levelmesh/wallcache.h"   // [rc4l] LevelGeneration

#include "MapHelper.hpp"
#include "c_cvars.h"
#include "v_video.h"   // [rc4l] screen, the size the engine draws at

EXTERN_CVAR(Int, r_texture_filter)

// [Doruk-Aksoy] The 3D and the 2D in 16-bit float, as UZDoom's scene and pipeline images are, so post-process shaders read light past 1, below 0 and between 8-bit steps; off draws both in the swapchain's 8 bits.
CVAR(Bool, fua_dg_scene16, true, CVAR_ARCHIVE)

namespace zx { namespace hwrender {

namespace {

Diligent::RefCntAutoPtr<Diligent::ITexture> g_frameColor, g_frameDepth;
int g_frameW = 0, g_frameH = 0;
unsigned int g_frameGen = 0;   // [rc4l] bumped each time the target is made, whose contents start undefined
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_blitPSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_blitSRB;
// [Doruk-Aksoy] A 16-bit frame copied to 8 bits for what reads it as bytes (fua_clip's recorder), made only while one does.
Diligent::RefCntAutoPtr<Diligent::ITexture> g_frame8;
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_frame8PSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_frame8SRB;

// [rc4l] features/dynres: the scene's own colour target at the frame's size, which a frame below full scale draws its 3D into the top left of, and the pixels it covers this frame.
Diligent::RefCntAutoPtr<Diligent::ITexture> g_sceneColor;
int g_sceneW = 0, g_sceneH = 0;
// [Doruk-Aksoy] features/postprocess: the 3D drawn into the scene target at full scale too, for its shaders to read while the 2D goes into the frame.
bool g_sceneSeparate = false;
// [rc4l] The upscale over the frame, by point and by linear filtering, and the share of the scene target it reads.
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_upscalePSO[2];
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_upscaleSRB[2];
Diligent::RefCntAutoPtr<Diligent::IBuffer> g_upscaleCB;

const char *kUpscalePS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 0) uniform Upscale { vec4 uScene; };\n"
	"layout(binding = 1) uniform texture2D uTex; uniform sampler uTex_sampler;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(sampler2D(uTex, uTex_sampler), vUV * uScene.xy).rgb, 1.0); }\n";

const char *kBlitPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 1) uniform texture2D uTex; uniform sampler uTex_sampler;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(sampler2D(uTex, uTex_sampler), vUV).rgb, 1.0); }\n";

// [rc4l] The size a frame is drawn at: the screen's in a launch without GL, else the window's.
void WantedSize(int &w, int &h)
{
	const auto &sd = GetSwapChain()->GetDesc();
	w = (int)sd.Width;
	h = (int)sd.Height;
	if (BackendOnly() && screen != NULL && screen->GetWidth() > 0 && screen->GetHeight() > 0)
	{
		w = screen->GetWidth();
		h = screen->GetHeight();
	}
}

// [Doruk-Aksoy] In the scene's format too, as UZDoom draws its 2D into its 16-bit pipeline image, made again when fua_dg_scene16 changes it.
bool EnsureFrameTarget()
{
	int w = 0, h = 0;
	WantedSize(w, h);
	if (g_frameColor && w == g_frameW && h == g_frameH && g_frameColor->GetDesc().Format == SceneColorFormat()) return true;
	g_blitSRB.Release();
	g_frame8SRB.Release();
	if (!CreateViewTextures(w, h, "fua frame", g_frameColor, g_frameDepth, SceneColorFormat()))
	{
		g_frameColor.Release();
		g_frameDepth.Release();
		g_frameW = g_frameH = 0;
		return false;
	}
	g_frameW = w;
	g_frameH = h;
	g_frameGen++;
	return true;
}

} // namespace

bool FrameOffscreen()
{
	return BackendOnly() && GetSwapChain() != NULL && EnsureFrameTarget();
}

Diligent::ITextureView *FrameRTV()
{
	if (FrameOffscreen()) return g_frameColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	return GetSwapChain()->GetCurrentBackBufferRTV();
}

Diligent::ITextureView *FrameDSV()
{
	if (FrameOffscreen()) return g_frameDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
	return GetSwapChain()->GetDepthBufferDSV();
}

int FrameWidth()
{
	return FrameOffscreen() ? g_frameW : (int)GetSwapChain()->GetDesc().Width;
}

int FrameHeight()
{
	return FrameOffscreen() ? g_frameH : (int)GetSwapChain()->GetDesc().Height;
}

void FinishFrame(Diligent::IDeviceContext *ctx)
{
	if (!FrameOffscreen()) return;
	// [rc4l] Linear, as GL's scale blit is.
	const auto &sd = GetSwapChain()->GetDesc();
	if (!g_blitPSO && !CreatePicturePSO("fua frame blit", kBlitPS, Diligent::FILTER_TYPE_LINEAR, Diligent::TEX_FORMAT_UNKNOWN, g_blitPSO)) return;
	if (!g_blitSRB && !CreatePictureBinding(g_blitPSO, "uTex", g_frameColor, g_blitSRB)) return;
	Diligent::ITextureView *rtv = GetSwapChain()->GetCurrentBackBufferRTV();
	ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, NULL, sd.Width, sd.Height);
	ctx->SetPipelineState(g_blitPSO);
	ctx->CommitShaderResources(g_blitSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = 3;
	ctx->Draw(draw);
}

unsigned int FrameGeneration() { return g_frameGen; }

namespace {

// [rc4l] The scene target at the frame's size, its upscale bindings made again with it.
// [Doruk-Aksoy] And in the scene's format, made again when fua_dg_scene16 changes it.
bool EnsureSceneColor()
{
	if (g_sceneColor && (int)g_sceneColor->GetDesc().Width == g_frameW && (int)g_sceneColor->GetDesc().Height == g_frameH &&
		g_sceneColor->GetDesc().Format == SceneColorFormat()) return true;
	g_upscaleSRB[0].Release();
	g_upscaleSRB[1].Release();
	return CreateColorTarget(g_frameW, g_frameH, "fua scene", g_sceneColor, SceneColorFormat());
}

// [rc4l] The upscale by point (0) or linear (1) filtering, its constants and its binding of the scene target.
bool EnsureUpscale(int linear)
{
	if (!g_upscaleCB)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua scene upscale constants";
		cbd.Size = sizeof(float) * 4;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		CreateDeviceBuffer(cbd, nullptr, &g_upscaleCB);
		if (!g_upscaleCB) return false;
	}
	// [Doruk-Aksoy] It draws into the frame, so a change of its format makes it again.
	if (g_upscalePSO[linear] && g_upscalePSO[linear]->GetGraphicsPipelineDesc().RTVFormats[0] != SceneColorFormat()) { g_upscaleSRB[linear].Release(); g_upscalePSO[linear].Release(); }
	if (!g_upscalePSO[linear])
	{
		if (!CreatePicturePSO(linear ? "fua scene upscale linear" : "fua scene upscale point", kUpscalePS,
			linear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT, Diligent::TEX_FORMAT_UNKNOWN, g_upscalePSO[linear], SceneColorFormat())) return false;
		if (auto *v = g_upscalePSO[linear]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Upscale")) v->Set(g_upscaleCB);
	}
	return g_upscaleSRB[linear] || CreatePictureBinding(g_upscalePSO[linear], "uTex", g_sceneColor, g_upscaleSRB[linear]);
}

}

void BeginSceneFrame(bool mayScale, bool separate)
{
	g_sceneW = FrameWidth();
	g_sceneH = FrameHeight();
	const bool canScale = mayScale && FrameOffscreen();
	const float scale = zx::dynres::ScaleForFrame(LastGpuPassMs(kGpuFrame), canScale, (unsigned)zx::levelmesh::LevelGeneration());
	g_sceneSeparate = separate && FrameOffscreen() && EnsureSceneColor();
	// [rc4l] Made while the scale is still full, so the first step down neither allocates nor compiles anything.
	if (!canScale || !zx::dynres::Active() || !EnsureSceneColor() || !EnsureUpscale(0) || !EnsureUpscale(1)) return;
	const zx::dynres::SceneRect r = zx::dynres::ComputeSceneRect(g_frameW, g_frameH, scale);
	g_sceneW = r.w;
	g_sceneH = r.h;
}

bool EnsureSceneTarget() { return FrameOffscreen() && EnsureSceneColor() && EnsureUpscale(0) && EnsureUpscale(1); }

bool Scene16() { return fua_dg_scene16 && BackendOnly(); }

Diligent::TEXTURE_FORMAT SceneColorFormat()
{
	return Scene16() ? Diligent::TEX_FORMAT_RGBA16_FLOAT : GetSwapChain()->GetDesc().ColorBufferFormat;
}

bool SceneScaled() { return g_sceneColor && (g_sceneW < g_frameW || g_sceneH < g_frameH); }
bool SceneSeparate() { return g_sceneSeparate || SceneScaled(); }
Diligent::ITexture *SceneTexture() { return SceneSeparate() ? g_sceneColor.RawPtr() : FrameTexture(); }
int SceneWidth() { return g_sceneW > 0 ? g_sceneW : FrameWidth(); }
int SceneHeight() { return g_sceneH > 0 ? g_sceneH : FrameHeight(); }

Diligent::ITextureView *SceneRTV()
{
	return SceneSeparate() ? g_sceneColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET) : FrameRTV();
}

void UpscaleScene(Diligent::IDeviceContext *ctx)
{
	if (!SceneSeparate()) return;
	const int linear = zx::hwrender::ComputeTextureFilter(r_texture_filter).magLinear ? 1 : 0;
	if (!EnsureUpscale(linear)) return;
	{
		Diligent::MapHelper<float> cb(ctx, g_upscaleCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		cb[0] = (float)g_sceneW / (float)g_frameW;
		cb[1] = (float)g_sceneH / (float)g_frameH;
		cb[2] = cb[3] = 0.f;
	}
	Diligent::ITextureView *rtv = FrameRTV();
	ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, NULL, (Diligent::Uint32)g_frameW, (Diligent::Uint32)g_frameH);
	ctx->SetPipelineState(g_upscalePSO[linear]);
	ctx->CommitShaderResources(g_upscaleSRB[linear], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = 3;
	ctx->Draw(draw);
}

Diligent::ITexture *FrameTexture()
{
	return FrameOffscreen() ? g_frameColor.RawPtr() : GetSwapChain()->GetCurrentBackBufferRTV()->GetTexture();
}

Diligent::ITexture *FrameAs8Bit(Diligent::IDeviceContext *ctx)
{
	Diligent::ITexture *frame = FrameTexture();
	if (frame == NULL || frame->GetDesc().Format != Diligent::TEX_FORMAT_RGBA16_FLOAT) return frame;
	const auto &fd = frame->GetDesc();
	if (!g_frame8 || g_frame8->GetDesc().Width != fd.Width || g_frame8->GetDesc().Height != fd.Height)
	{
		g_frame8SRB.Release();
		if (!CreateColorTarget((int)fd.Width, (int)fd.Height, "fua frame 8-bit", g_frame8, Diligent::TEX_FORMAT_RGBA8_UNORM)) return NULL;
	}
	if (!g_frame8PSO && !CreatePicturePSO("fua frame 8-bit", kBlitPS, Diligent::FILTER_TYPE_POINT, Diligent::TEX_FORMAT_UNKNOWN, g_frame8PSO, Diligent::TEX_FORMAT_RGBA8_UNORM)) return NULL;
	if (!g_frame8SRB && !CreatePictureBinding(g_frame8PSO, "uTex", frame, g_frame8SRB)) return NULL;
	Diligent::ITextureView *rtv = g_frame8->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);
	ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetViewports(1, NULL, fd.Width, fd.Height);
	ctx->SetPipelineState(g_frame8PSO);
	ctx->CommitShaderResources(g_frame8SRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = 3;
	ctx->Draw(draw);
	return g_frame8;
}

void ReleaseFrameTarget()
{
	g_frame8SRB.Release();
	g_frame8PSO.Release();
	g_frame8.Release();
	g_frameColor.Release();
	g_frameDepth.Release();
	g_frameW = g_frameH = 0;
	g_blitSRB.Release();
	g_blitPSO.Release();
	g_sceneColor.Release();
	g_sceneW = g_sceneH = 0;
	for (int k = 0; k < 2; k++) { g_upscaleSRB[k].Release(); g_upscalePSO[k].Release(); }
	g_upscaleCB.Release();
}

}} // namespace zx::hwrender
