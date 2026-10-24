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

namespace zx { namespace hwrender {

namespace {

Diligent::RefCntAutoPtr<Diligent::ITexture> g_frameColor, g_frameDepth;
int g_frameW = 0, g_frameH = 0;
unsigned int g_frameGen = 0;   // [rc4l] bumped each time the target is made, whose contents start undefined
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_blitPSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_blitSRB;

// [rc4l] features/dynres: the scene's own colour target at the frame's size, which a frame below full scale draws its 3D into the top left of, and the pixels it covers this frame.
Diligent::RefCntAutoPtr<Diligent::ITexture> g_sceneColor;
int g_sceneW = 0, g_sceneH = 0;
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

bool EnsureFrameTarget()
{
	int w = 0, h = 0;
	WantedSize(w, h);
	if (g_frameColor && w == g_frameW && h == g_frameH) return true;
	g_blitSRB.Release();
	if (!CreateViewTextures(w, h, "fua frame", g_frameColor, g_frameDepth))
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
bool EnsureSceneColor()
{
	if (g_sceneColor && (int)g_sceneColor->GetDesc().Width == g_frameW && (int)g_sceneColor->GetDesc().Height == g_frameH) return true;
	g_upscaleSRB[0].Release();
	g_upscaleSRB[1].Release();
	return CreateColorTarget(g_frameW, g_frameH, "fua scene", g_sceneColor);
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
	if (!g_upscalePSO[linear])
	{
		if (!CreatePicturePSO(linear ? "fua scene upscale linear" : "fua scene upscale point", kUpscalePS,
			linear ? Diligent::FILTER_TYPE_LINEAR : Diligent::FILTER_TYPE_POINT, Diligent::TEX_FORMAT_UNKNOWN, g_upscalePSO[linear])) return false;
		if (auto *v = g_upscalePSO[linear]->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Upscale")) v->Set(g_upscaleCB);
	}
	return g_upscaleSRB[linear] || CreatePictureBinding(g_upscalePSO[linear], "uTex", g_sceneColor, g_upscaleSRB[linear]);
}

}

void BeginSceneFrame(bool mayScale)
{
	g_sceneW = FrameWidth();
	g_sceneH = FrameHeight();
	const bool canScale = mayScale && FrameOffscreen();
	const float scale = zx::dynres::ScaleForFrame(LastGpuPassMs(kGpuFrame), canScale, (unsigned)zx::levelmesh::LevelGeneration());
	// [rc4l] Made while the scale is still full, so the first step down neither allocates nor compiles anything.
	if (!canScale || !zx::dynres::Active() || !EnsureSceneColor() || !EnsureUpscale(0) || !EnsureUpscale(1)) return;
	const zx::dynres::SceneRect r = zx::dynres::ComputeSceneRect(g_frameW, g_frameH, scale);
	g_sceneW = r.w;
	g_sceneH = r.h;
}

bool SceneScaled() { return g_sceneColor && (g_sceneW < g_frameW || g_sceneH < g_frameH); }
int SceneWidth() { return g_sceneW > 0 ? g_sceneW : FrameWidth(); }
int SceneHeight() { return g_sceneH > 0 ? g_sceneH : FrameHeight(); }

Diligent::ITextureView *SceneRTV()
{
	return SceneScaled() ? g_sceneColor->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET) : FrameRTV();
}

void UpscaleScene(Diligent::IDeviceContext *ctx)
{
	if (!SceneScaled()) return;
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

void ReleaseFrameTarget()
{
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
