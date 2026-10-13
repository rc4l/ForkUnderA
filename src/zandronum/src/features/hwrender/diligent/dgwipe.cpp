// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The screen wipes in a launch without GL: the last frame and the new one kept as pictures, and each tic's mix of them drawn under the console and menus, as GL's wipers draw it (features/wipe works the mix out).

#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/wipe/computation/wipe_compute.h"

#include "f_wipe.h"     // [rc4l] the wipe types and wipe_CalcBurn, the fire the burn's mask is taken from
#include "m_random.h"
#include "MapHelper.hpp"

#include <string.h>
#include <vector>

namespace zx { namespace hwrender {

namespace {

const int kBurnSize = 64;

int g_type = wipe_None;
int g_melt[zx::wipe::kMeltColumns];
int g_clock = 0, g_density = 4;
unsigned char g_fire[kBurnSize * (kBurnSize + 5)];
unsigned char g_mask[kBurnSize * kBurnSize];
Diligent::RefCntAutoPtr<Diligent::ITexture> g_start, g_end, g_maskTex, g_white;
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_wipePSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_srbStart, g_srbEnd, g_srbEndMasked;
Diligent::RefCntAutoPtr<Diligent::IBuffer> g_wipeCB, g_vb;
unsigned g_vbCapacity = 0;

// [rc4l] A wipe vertex in screen pixels: where it lands, the screen picture's coordinate and its opacity.
struct WipeVertex { float x, y, u, v, a; };

const char *kWipeVS =
	"#version 450\n"
	"layout(location = 0) in vec2 aPos;\n"
	"layout(location = 1) in vec2 aUV;\n"
	"layout(location = 2) in float aAlpha;\n"
	"layout(binding = 0) uniform Screen { vec4 uInvScreen; };\n"
	"layout(location = 0) out vec2 vUV;\n"
	"layout(location = 1) out vec2 vMaskUV;\n"
	"layout(location = 2) out float vAlpha;\n"
	"void main() {\n"
	"    vUV = aUV;\n"
	"    vMaskUV = aPos * uInvScreen.xy;\n"
	"    vAlpha = aAlpha;\n"
	"    gl_Position = vec4(aPos.x * uInvScreen.x * 2.0 - 1.0, 1.0 - aPos.y * uInvScreen.y * 2.0, 0.0, 1.0);\n"
	"}\n";

// [rc4l] The picture, its opacity scaled by the mask's, which is white but for the burn.
const char *kWipePS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(location = 1) in vec2 vMaskUV;\n"
	"layout(location = 2) in float vAlpha;\n"
	"layout(binding = 1) uniform sampler2D uScreen;\n"
	"layout(binding = 2) uniform sampler2D uMask;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(texture(uScreen, vUV).rgb, vAlpha * texture(uMask, vMaskUV).a); }\n";

bool CreateCopyOfFrame(Diligent::IDeviceContext *ctx, const char *name, Diligent::RefCntAutoPtr<Diligent::ITexture> &out)
{
	Diligent::ITexture *frame = FrameTexture();
	if (frame == NULL) return false;
	Diligent::TextureDesc td = frame->GetDesc();
	td.Name = name;
	td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	td.Usage = Diligent::USAGE_DEFAULT;
	out.Release();
	GetDevice()->CreateTexture(td, nullptr, &out);
	if (!out) return false;
	ctx->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_NONE);
	Diligent::CopyTextureAttribs cta;
	cta.pSrcTexture = frame;
	cta.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	cta.pDstTexture = out;
	cta.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
	ctx->CopyTexture(cta);
	return true;
}

bool CreateMaskTexture(const char *name, const unsigned char *alpha, int w, int h, Diligent::USAGE usage, Diligent::RefCntAutoPtr<Diligent::ITexture> &out)
{
	std::vector<unsigned char> rgba((size_t)w * h * 4, 255);
	for (int i = 0; i < w * h; i++) rgba[i * 4 + 3] = alpha[i];
	Diligent::TextureDesc td;
	td.Name = name;
	td.Type = Diligent::RESOURCE_DIM_TEX_2D;
	td.Width = w;
	td.Height = h;
	td.MipLevels = 1;
	td.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
	td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
	td.Usage = usage;
	Diligent::TextureSubResData sub;
	sub.pData = &rgba[0];
	sub.Stride = w * 4;
	Diligent::TextureData data;
	data.pSubResources = &sub;
	data.NumSubresources = 1;
	out.Release();
	GetDevice()->CreateTexture(td, &data, &out);
	return out.RawPtr() != nullptr;
}

bool EnsurePipeline()
{
	if (g_wipePSO) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;
	Diligent::RefCntAutoPtr<Diligent::IShader> vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua wipe VS", kWipeVS);
	Diligent::RefCntAutoPtr<Diligent::IShader> ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua wipe PS", kWipePS);
	if (!vs || !ps) return false;
	Diligent::BufferDesc cbd;
	cbd.Name = "fua wipe constants";
	cbd.Size = sizeof(float) * 4;
	cbd.Usage = Diligent::USAGE_DYNAMIC;
	cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
	cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
	dev->CreateBuffer(cbd, nullptr, &g_wipeCB);
	if (!g_wipeCB) return false;
	Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 2, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},
	};
	Diligent::ShaderResourceVariableDesc vars[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uScreen", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		{ Diligent::SHADER_TYPE_PIXEL, "uMask", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
	};
	// [rc4l] The screens point-sampled, being the frame's own size; the burn's 64 by 64 mask stretched smoothly over it, as GL's is.
	Diligent::SamplerDesc point, linear;
	point.MinFilter = point.MagFilter = point.MipFilter = Diligent::FILTER_TYPE_POINT;
	point.AddressU = point.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
	linear = point;
	linear.MinFilter = linear.MagFilter = linear.MipFilter = Diligent::FILTER_TYPE_LINEAR;
	Diligent::ImmutableSamplerDesc samplers[] = {
		{ Diligent::SHADER_TYPE_PIXEL, "uScreen", point },
		{ Diligent::SHADER_TYPE_PIXEL, "uMask", linear },
	};
	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua wipe PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
	auto &rt = pci.GraphicsPipeline.BlendDesc.RenderTargets[0];
	rt.BlendEnable = true;
	rt.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
	rt.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
	rt.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
	rt.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
	pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
	pci.GraphicsPipeline.InputLayout.NumElements = 3;
	pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
	pci.PSODesc.ResourceLayout.Variables = vars;
	pci.PSODesc.ResourceLayout.NumVariables = 2;
	pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
	pci.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
	pci.pVS = vs;
	pci.pPS = ps;
	dev->CreateGraphicsPipelineState(pci, &g_wipePSO);
	if (!g_wipePSO) return false;
	if (auto *v = g_wipePSO->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Screen")) v->Set(g_wipeCB);
	const unsigned char opaque = 255;
	return CreateMaskTexture("fua wipe white", &opaque, 1, 1, Diligent::USAGE_IMMUTABLE, g_white);
}

bool Bind(Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> &srb, Diligent::ITexture *screen, Diligent::ITexture *mask)
{
	srb.Release();
	g_wipePSO->CreateShaderResourceBinding(&srb, true);
	if (!srb) return false;
	srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uScreen")->Set(screen->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uMask")->Set(mask->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	return true;
}

// [rc4l] A rectangle of a screen picture, x and y in pixels, u and v across the picture from its top left.
void PushRect(std::vector<WipeVertex> &vb, float x, float y, float w, float h, float u1, float v1, float u2, float v2, float a)
{
	const WipeVertex c[4] = { { x, y, u1, v1, a }, { x, y + h, u1, v2, a }, { x + w, y, u2, v1, a }, { x + w, y + h, u2, v2, a } };
	vb.push_back(c[0]); vb.push_back(c[1]); vb.push_back(c[2]);
	vb.push_back(c[2]); vb.push_back(c[1]); vb.push_back(c[3]);
}

void DrawRange(Diligent::IDeviceContext *ctx, Diligent::IShaderResourceBinding *srb, unsigned first, unsigned count)
{
	if (count == 0) return;
	ctx->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = count;
	draw.StartVertexLocation = first;
	ctx->Draw(draw);
}

} // namespace

bool WipeStart(int type)
{
	auto *ctx = GetContext();
	if (ctx == NULL || !FrameOffscreen() || (type != wipe_Melt && type != wipe_Burn && type != wipe_Fade)) return false;
	if (!EnsurePipeline() || !CreateCopyOfFrame(ctx, "fua wipe start", g_start)) return false;
	g_type = type;
	g_clock = 0;
	g_density = 4;
	memset(g_fire, 0, sizeof(g_fire));
	memset(g_mask, 0, sizeof(g_mask));
	if (type == wipe_Melt)
	{
		unsigned char randoms[zx::wipe::kMeltColumns];
		for (int i = 0; i < zx::wipe::kMeltColumns; i++) randoms[i] = (unsigned char)M_Random();
		zx::wipe::ComputeMeltStart(randoms, g_melt);
	}
	g_end.Release();
	return true;
}

bool WipeActive() { return g_type != wipe_None && g_end.RawPtr() != nullptr; }

void WipeTakeEnd()
{
	auto *ctx = GetContext();
	if (ctx == NULL || g_type == wipe_None || !CreateCopyOfFrame(ctx, "fua wipe end", g_end)) return;
	if (!Bind(g_srbStart, g_start, g_white) || !Bind(g_srbEnd, g_end, g_white)) g_end.Release();
}

bool WipeTic(int ticks)
{
	if (!WipeActive()) return true;
	switch (g_type)
	{
	case wipe_Melt:
	{
		bool done = false;
		while (ticks-- > 0) done = zx::wipe::ComputeMeltTic(g_melt);
		return done;
	}
	case wipe_Fade:
		g_clock += ticks;
		return g_clock >= 32;
	default:
	{
		// [rc4l] The fire twice a tic, ended at 40 tics in case it never settles, as GL burns it.
		g_clock += ticks;
		bool done = false;
		for (int n = ticks * 2; !done && n-- > 0; )
		{
			g_density = wipe_CalcBurn(g_fire, kBurnSize, kBurnSize, g_density);
			done = g_density < 0;
		}
		zx::wipe::ComputeBurnMask(g_fire, kBurnSize * kBurnSize, g_mask);
		if (!CreateMaskTexture("fua wipe burn", g_mask, kBurnSize, kBurnSize, Diligent::USAGE_IMMUTABLE, g_maskTex) ||
			!Bind(g_srbEndMasked, g_end, g_maskTex)) return true;
		return done || g_clock > 40;
	}
	}
}

void WipeDraw(Diligent::IDeviceContext *ctx)
{
	if (!WipeActive()) return;
	const int w = FrameWidth(), h = FrameHeight();
	std::vector<WipeVertex> vb;
	unsigned startFirst = 0, startCount = 0, endFirst = 0, endCount = 0;
	Diligent::IShaderResourceBinding *endSrb = g_srbEnd;
	if (g_type == wipe_Melt)
	{
		// [rc4l] The new screen underneath, the old one's columns falling away over it.
		PushRect(vb, 0, 0, (float)w, (float)h, 0, 0, 1, 1, 1);
		endCount = 6;
		startFirst = 6;
		for (int i = 0; i < zx::wipe::kMeltColumns; i++)
		{
			zx::wipe::MeltStrip s;
			if (zx::wipe::ComputeMeltStrip(i, g_melt[i], w, h, s)) PushRect(vb, s.x, s.y, s.w, s.h, s.u1, 0, s.u2, s.v2, 1);
		}
		startCount = (unsigned)vb.size() - 6;
	}
	else
	{
		// [rc4l] The old screen underneath, the new one over it faded in or burnt through.
		PushRect(vb, 0, 0, (float)w, (float)h, 0, 0, 1, 1, 1);
		startCount = 6;
		endFirst = 6;
		const float a = g_type == wipe_Fade ? zx::wipe::ComputeCrossfadeAlpha(g_clock) : 1.f;
		PushRect(vb, 0, 0, (float)w, (float)h, 0, 0, 1, 1, a);
		endCount = 6;
		if (g_type == wipe_Burn)
		{
			if (!g_srbEndMasked) return;
			endSrb = g_srbEndMasked;
		}
	}
	if (!UploadGrowingVB(ctx, g_vb, g_vbCapacity, &vb[0], (unsigned)vb.size(), sizeof(WipeVertex), 64, "fua wipe VB")) return;
	{
		Diligent::MapHelper<float> cb(ctx, g_wipeCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		cb[0] = 1.0f / w; cb[1] = 1.0f / h; cb[2] = 0.f; cb[3] = 0.f;
	}
	Diligent::IBuffer *vbs[] = { g_vb };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	ctx->SetPipelineState(g_wipePSO);
	// [rc4l] Painter's order: what lies underneath first.
	if (g_type == wipe_Melt)
	{
		DrawRange(ctx, endSrb, endFirst, endCount);
		DrawRange(ctx, g_srbStart, startFirst, startCount);
	}
	else
	{
		DrawRange(ctx, g_srbStart, startFirst, startCount);
		DrawRange(ctx, endSrb, endFirst, endCount);
	}
}

void WipeCleanup()
{
	g_type = wipe_None;
	g_srbStart.Release();
	g_srbEnd.Release();
	g_srbEndMasked.Release();
	g_start.Release();
	g_end.Release();
	g_maskTex.Release();
}

}} // namespace zx::hwrender
