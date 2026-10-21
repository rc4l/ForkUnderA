// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fua_dg_gpuload: GPU work on demand over the scene's pixels, writing nothing, so a frame the GPU holds can be made on any machine (features/dynres), as fua_debug_framestall_us makes one the CPU holds.

#include "features/hwrender/diligent/dgscene_internal.h"

#include "MapHelper.hpp"
#include "c_cvars.h"

// [rc4l] Loops of shader work at each of the scene's pixels, 0 for none.
CVAR(Int, fua_dg_gpuload, 0, 0)

namespace zx { namespace hwrender {

namespace {

Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_loadPSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_loadSRB;
Diligent::RefCntAutoPtr<Diligent::IBuffer> g_loadCB;

// [rc4l] uLoad.x loops of arithmetic the compiler cannot drop, compared with uLoad.y (never reached) so every fragment discards.
const char *kLoadPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 0) uniform Load { vec4 uLoad; };\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    float acc = vUV.x + vUV.y;\n"
	"    for (int i = 0; i < int(uLoad.x); i++) acc = fract(sin(acc * 12.9898 + float(i)) * 43758.5453);\n"
	"    if (acc != uLoad.y) discard;\n"
	"    outColor = vec4(acc);\n"
	"}\n";

bool EnsureLoad()
{
	if (!g_loadCB)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua gpu load constants";
		cbd.Size = sizeof(float) * 4;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		CreateDeviceBuffer(cbd, nullptr, &g_loadCB);
		if (!g_loadCB) return false;
	}
	if (!g_loadPSO)
	{
		if (!CreatePicturesPSO("fua gpu load", kLoadPS, Diligent::FILTER_TYPE_POINT, Diligent::TEX_FORMAT_UNKNOWN, nullptr, 0, g_loadPSO)) return false;
		if (auto *v = g_loadPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Load")) v->Set(g_loadCB);
		g_loadPSO->CreateShaderResourceBinding(&g_loadSRB, true);
	}
	return g_loadSRB.RawPtr() != nullptr;
}

}

void DrawGpuLoad(Diligent::IDeviceContext *ctx)
{
	if (fua_dg_gpuload <= 0 || !EnsureLoad()) return;
	{
		Diligent::MapHelper<float> cb(ctx, g_loadCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		cb[0] = (float)fua_dg_gpuload;
		cb[1] = -1.f;
		cb[2] = cb[3] = 0.f;
	}
	Diligent::ITextureView *rtv = SceneRTV();
	ctx->SetRenderTargets(1, &rtv, NULL, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::Viewport vp;
	vp.Width = (float)SceneWidth(); vp.Height = (float)SceneHeight();
	vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
	ctx->SetViewports(1, &vp, (Diligent::Uint32)FrameWidth(), (Diligent::Uint32)FrameHeight());
	ctx->SetPipelineState(g_loadPSO);
	ctx->CommitShaderResources(g_loadSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs draw;
	draw.NumVertices = 3;
	ctx->Draw(draw);
}

void ReleaseGpuLoad()
{
	g_loadSRB.Release();
	g_loadPSO.Release();
	g_loadCB.Release();
}

}} // namespace zx::hwrender
