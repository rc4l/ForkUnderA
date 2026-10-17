// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where a frame is drawn: beside GL the window's own back buffer, and in a launch without GL an offscreen target at the screen's size, drawn up to the window at present as GL's scale buffer is, which keeps the last frame for a wipe.

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/backendonly.h"   // [rc4l] BackendOnly

#include "v_video.h"   // [rc4l] screen, the size the engine draws at

namespace zx { namespace hwrender {

namespace {

Diligent::RefCntAutoPtr<Diligent::ITexture> g_frameColor, g_frameDepth;
int g_frameW = 0, g_frameH = 0;
unsigned int g_frameGen = 0;   // [rc4l] bumped each time the target is made, whose contents start undefined
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_blitPSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_blitSRB;

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
}

}} // namespace zx::hwrender
