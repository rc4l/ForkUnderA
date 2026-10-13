// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] GL's stereo 3D (gl/stereo3d, vr_mode) for the backend: the eye BuildMVP shifts the camera and leans the frustum for, and an anaglyph's two eyes blended through their colour masks under the 2D.

#ifdef FUA_DILIGENT

#include "features/hwrender/diligent/dgshared.h"
#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/computation/stereo_compute.h"
#include "features/viewsetup/viewsetup.h"   // [rc4l] ComputeViewAngleVector, GL's yaw
#include "r_utility.h"   // [rc4l] viewangle
#include "c_cvars.h"

EXTERN_CVAR(Int, vr_mode)
EXTERN_CVAR(Float, vr_ipd)
EXTERN_CVAR(Float, vr_screendist)
EXTERN_CVAR(Float, vr_hunits_per_meter)

namespace zx { namespace hwrender {

namespace {

// [rc4l] The eye being drawn, in interpupillary distances (0 for the middle).
float g_eyeShift = 0.f;

// [rc4l] An anaglyph's first eye, kept while the second draws over the frame, and the second's.
Diligent::RefCntAutoPtr<Diligent::ITexture> g_eyePicture[2];
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_blendPSO;
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_blendSRB;
Diligent::RefCntAutoPtr<Diligent::IBuffer> g_blendCB;

const char *kBlendVS =
	"#version 450\n"
	"layout(location = 0) out vec2 vUV;\n"
	"void main() {\n"
	"    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
	"    vUV = vec2(p.x, 1.0 - p.y);\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

// [rc4l] Each eye's channels through its mask, as GL's colour mask leaves them in the one frame.
const char *kBlendPS =
	"#version 450\n"
	"layout(location = 0) in vec2 vUV;\n"
	"layout(binding = 0) uniform Masks { vec4 uLeftMask; vec4 uRightMask; };\n"
	"layout(binding = 1) uniform sampler2D uLeft;\n"
	"layout(binding = 2) uniform sampler2D uRight;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() {\n"
	"    outColor = vec4(texture(uLeft, vUV).rgb * uLeftMask.rgb + texture(uRight, vUV).rgb * uRightMask.rgb, 1.0);\n"
	"}\n";

bool EnsureEyePictures(int w, int h)
{
	auto *dev = GetDevice();
	auto *frame = FrameTexture();
	if (!dev || !frame) return false;
	const auto &fd = frame->GetDesc();
	for (int e = 0; e < 2; e++)
	{
		if (g_eyePicture[e] && g_eyePicture[e]->GetDesc().Width == (Diligent::Uint32)w && g_eyePicture[e]->GetDesc().Height == (Diligent::Uint32)h) continue;
		g_blendSRB.Release();
		g_eyePicture[e].Release();
		Diligent::TextureDesc td;
		td.Name = e == 0 ? "fua left eye" : "fua right eye";
		td.Type = Diligent::RESOURCE_DIM_TEX_2D;
		td.Width = w; td.Height = h;
		td.Format = fd.Format;
		td.BindFlags = Diligent::BIND_SHADER_RESOURCE;
		dev->CreateTexture(td, nullptr, &g_eyePicture[e]);
		if (!g_eyePicture[e]) return false;
	}
	if (!g_blendPSO)
	{
		auto vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua stereo blend VS", kBlendVS);
		auto ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua stereo blend PS", kBlendPS);
		if (!vs || !ps) return false;
		Diligent::BufferDesc cbd;
		cbd.Name = "fua stereo masks";
		cbd.Size = sizeof(float) * 8;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		dev->CreateBuffer(cbd, nullptr, &g_blendCB);
		if (!g_blendCB) return false;
		const Diligent::ShaderResourceVariableDesc vars[] = {
			{ Diligent::SHADER_TYPE_PIXEL, "uLeft", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
			{ Diligent::SHADER_TYPE_PIXEL, "uRight", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE },
		};
		Diligent::SamplerDesc samp;
		samp.MinFilter = samp.MagFilter = samp.MipFilter = Diligent::FILTER_TYPE_POINT;
		samp.AddressU = samp.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
		const Diligent::ImmutableSamplerDesc samplers[] = { { Diligent::SHADER_TYPE_PIXEL, "uLeft", samp }, { Diligent::SHADER_TYPE_PIXEL, "uRight", samp } };
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = "fua stereo blend PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = fd.Format;
		pci.GraphicsPipeline.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.PSODesc.ResourceLayout.Variables = vars;
		pci.PSODesc.ResourceLayout.NumVariables = 2;
		pci.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
		pci.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
		pci.pVS = vs;
		pci.pPS = ps;
		dev->CreateGraphicsPipelineState(pci, &g_blendPSO);
		if (!g_blendPSO) return false;
		if (auto *v = g_blendPSO->GetStaticVariableByName(Diligent::SHADER_TYPE_PIXEL, "Masks")) v->Set(g_blendCB);
	}
	if (!g_blendSRB)
	{
		g_blendPSO->CreateShaderResourceBinding(&g_blendSRB, true);
		if (!g_blendSRB) return false;
		if (auto *v = g_blendSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uLeft")) v->Set(g_eyePicture[0]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
		if (auto *v = g_blendSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "uRight")) v->Set(g_eyePicture[1]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE));
	}
	return true;
}

} // namespace

StereoMode CurrentStereoMode() { return ComputeStereoMode(vr_mode); }

void SetStereoEye(float shift) { g_eyeShift = shift; }

void ShiftCameraForEye(BackendCamera &c, float &ndcShift)
{
	ndcShift = 0.f;
	if (g_eyeShift == 0.f) return;
	const float shiftMeters = g_eyeShift * vr_ipd;
	float yaw, vx, vy, dx, dy;
	zx::viewsetup::ComputeViewAngleVector(viewangle, yaw, vx, vy);
	ComputeEyeOffset(shiftMeters, yaw, vr_hunits_per_meter, dx, dy);
	c.x += dx;
	c.y += dy;
	ndcShift = ComputeEyeNdcShift(shiftMeters, vr_screendist, c.fovY, c.aspect);
}

void KeepEyePicture(Diligent::IDeviceContext *ctx, int eye)
{
	if (eye < 0 || eye > 1 || !EnsureEyePictures(FrameWidth(), FrameHeight())) return;
	Diligent::CopyTextureAttribs copy(FrameTexture(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
		g_eyePicture[eye], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->CopyTexture(copy);
}

void BlendEyes(Diligent::IDeviceContext *ctx, const StereoMode &mode)
{
	if (!g_eyePicture[0] || !g_eyePicture[1] || !g_blendSRB) return;
	{
		Diligent::MapHelper<float> cb(ctx, g_blendCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int c = 0; c < 3; c++) { cb[c] = mode.mask[0][c]; cb[4 + c] = mode.mask[1][c]; }
		cb[3] = cb[7] = 0.f;
	}
	for (int e = 0; e < 2; e++)
	{
		Diligent::StateTransitionDesc barrier(g_eyePicture[e], Diligent::RESOURCE_STATE_UNKNOWN, Diligent::RESOURCE_STATE_SHADER_RESOURCE,
			Diligent::STATE_TRANSITION_FLAG_UPDATE_STATE);
		ctx->TransitionResourceStates(1, &barrier);
	}
	auto *rtv = FrameRTV();
	ctx->SetRenderTargets(1, &rtv, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	ctx->SetPipelineState(g_blendPSO);
	ctx->CommitShaderResources(g_blendSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_VERIFY);
	Diligent::DrawAttribs d;
	d.NumVertices = 3;
	d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(d);
}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
