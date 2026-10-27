// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] World-space lines over the frame, each a screen-space quad of a width in pixels (wideline_compute): the hitbox view's boxes, which GL draws as GL_LINES under glLineWidth.

#ifdef FUA_DILIGENT

#include "features/hwrender/diligent/dgshared.h"
#include "features/hwrender/diligent/dgscene_internal.h"
#include "features/hwrender/computation/wideline_compute.h"
#include "features/hitboxviz/hitboxviz.h"
#include "tarray.h"

namespace zx { namespace hwrender {

namespace {

// [rc4l] One corner of a line's quad: both its ends in mesh space, which end and side this corner is, and its colour.
struct LineVertex { float start[3], end[3], at, side, rgb[3]; };

// [rc4l] The pass's constants: the view-projection, then the screen's size and the width in pixels.
#define FUA_LINES_CB_GLSL "layout(binding = 0) uniform Lines { mat4 uMVP; vec4 uScreen; };\n"

// [rc4l] ComputeWideLineCorner, line for line, with kWideLineNearW's 0.0001.
const char *kLinesVS =
	"#version 450\n"
	"layout(location = 0) in vec3 aStart;\n"
	"layout(location = 1) in vec3 aEnd;\n"
	"layout(location = 2) in float aAt;\n"
	"layout(location = 3) in float aSide;\n"
	"layout(location = 4) in vec3 aColor;\n"
	FUA_LINES_CB_GLSL
	"layout(location = 0) flat out vec3 vColor;\n"
	"void main() {\n"
	"    const float nearW = 0.0001;\n"
	"    vColor = aColor;\n"
	"    vec4 s = uMVP * vec4(aStart, 1.0), e = uMVP * vec4(aEnd, 1.0);\n"
	"    if (s.w < nearW && e.w < nearW) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
	"    if (s.w < nearW) s += (e - s) * ((nearW - s.w) / (e.w - s.w));\n"
	"    if (e.w < nearW) e += (s - e) * ((nearW - e.w) / (s.w - e.w));\n"
	"    vec2 halfScreen = uScreen.xy * 0.5;\n"
	"    vec2 d = (e.xy / e.w - s.xy / s.w) * halfScreen;\n"
	"    float len = length(d);\n"
	"    vec2 n = len > 1e-6 ? vec2(-d.y, d.x) / len : vec2(0.0, 1.0);\n"
	"    vec4 p = aAt > 0.5 ? e : s;\n"
	"    p.xy += n * aSide * uScreen.z * 0.5 / halfScreen * p.w;\n"
	"    gl_Position = p;\n"
	"}\n";

const char *kLinesPS =
	"#version 450\n"
	"layout(location = 0) flat in vec3 vColor;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(vColor, 1.0); }\n";

// [rc4l] Index 0 tests depth as GL's glDepthFunc(GL_LESS) does, 1 is cl_fua_hitbox_xray's, which ignores it.
Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_linePSO[2];
Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> g_lineSRB[2];
Diligent::RefCntAutoPtr<Diligent::IBuffer> g_lineVB, g_lineCB;
unsigned int g_lineVBCapacity = 0;

bool EnsureLinePipelines()
{
	// [Doruk-Aksoy] They draw into the scene, so a change of its format (fua_dg_scene16) makes them again.
	if (g_linePSO[0] && g_linePSO[0]->GetGraphicsPipelineDesc().RTVFormats[0] != SceneColorFormat())
		for (int k = 0; k < 2; k++) { g_lineSRB[k].Release(); g_linePSO[k].Release(); }
	if (g_linePSO[0] && g_linePSO[1]) return true;
	auto *dev = GetDevice();
	auto *swap = GetSwapChain();
	if (!dev || !swap) return false;
	Diligent::RefCntAutoPtr<Diligent::IShader> vs = CompileShader(Diligent::SHADER_TYPE_VERTEX, "fua lines VS", kLinesVS);
	Diligent::RefCntAutoPtr<Diligent::IShader> ps = CompileShader(Diligent::SHADER_TYPE_PIXEL, "fua lines PS", kLinesPS);
	if (!vs || !ps) return false;
	if (!g_lineCB)
	{
		Diligent::BufferDesc cbd;
		cbd.Name = "fua lines constants";
		cbd.Size = sizeof(float) * 20;
		cbd.Usage = Diligent::USAGE_DYNAMIC;
		cbd.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
		cbd.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
		CreateDeviceBuffer(cbd, nullptr, &g_lineCB);
		if (!g_lineCB) return false;
	}
	const Diligent::LayoutElement layout[] = {
		Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{1, 0, 3, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{2, 0, 1, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{3, 0, 1, Diligent::VT_FLOAT32, false},
		Diligent::LayoutElement{4, 0, 3, Diligent::VT_FLOAT32, false},
	};
	for (int xray = 0; xray < 2; xray++)
	{
		Diligent::GraphicsPipelineStateCreateInfo pci;
		pci.PSODesc.Name = xray ? "fua lines xray PSO" : "fua lines PSO";
		pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
		ClampDepthLikeGL(pci.GraphicsPipeline);
		pci.GraphicsPipeline.NumRenderTargets = 1;
		pci.GraphicsPipeline.RTVFormats[0] = SceneColorFormat();
		pci.GraphicsPipeline.DSVFormat = swap->GetDesc().DepthBufferFormat;
		pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
		pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = xray == 0;
		pci.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
		pci.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS;
		pci.GraphicsPipeline.InputLayout.LayoutElements = layout;
		pci.GraphicsPipeline.InputLayout.NumElements = sizeof(layout) / sizeof(layout[0]);
		pci.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
		pci.pVS = vs;
		pci.pPS = ps;
		CreatePipeline(pci, &g_linePSO[xray]);
		if (!g_linePSO[xray]) return false;
		if (auto *v = g_linePSO[xray]->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "Lines")) v->Set(g_lineCB);
		g_linePSO[xray]->CreateShaderResourceBinding(&g_lineSRB[xray], true);
		if (!g_lineSRB[xray]) return false;
	}
	return true;
}

} // namespace

void DrawHitboxes(Diligent::IDeviceContext *ctx)
{
	zx::hitboxviz::LineBatch batches[zx::hitboxviz::kLineBatches];
	bool xray = false;
	float widthPx = 1.f;
	const int n = zx::hitboxviz::BackendLines(batches, xray, widthPx);
	const float *mvp = SceneMVP();
	if (n <= 0 || mvp == NULL || !EnsureLinePipelines()) return;

	static TArray<LineVertex> verts;
	verts.Clear();
	for (int b = 0; b < n; b++)
	{
		const float rgb[3] = { ((batches[b].rgb >> 16) & 0xff) / 255.f, ((batches[b].rgb >> 8) & 0xff) / 255.f, (batches[b].rgb & 0xff) / 255.f };
		for (unsigned int i = 0; i + 1 < batches[b].count; i += 2)
		{
			const zx::hitboxviz::Vertex3 &s = batches[b].verts[i], &e = batches[b].verts[i + 1];
			for (int k = 0; k < 6; k++)
			{
				// [rc4l] Mesh space is (x, up, y), as FFlatVertex::Set takes GL's.
				LineVertex v = { { s.x, s.z, s.y }, { e.x, e.z, e.y }, 0.f, 0.f, { rgb[0], rgb[1], rgb[2] } };
				ComputeWideLineVertex(k, v.at, v.side);
				verts.Push(v);
			}
		}
	}
	if (verts.Size() == 0) return;
	if (!UploadGrowingVB(ctx, g_lineVB, g_lineVBCapacity, &verts[0], verts.Size(), sizeof(LineVertex), 1024, "fua lines VB")) return;
	{
		Diligent::MapHelper<float> cb(ctx, g_lineCB, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
		for (int i = 0; i < 16; i++) cb[i] = mvp[i];
		// [rc4l] In the scene's pixels, a line keeping its width on the screen when the scene is drawn below full scale (features/dynres).
		cb[16] = (float)SceneWidth(); cb[17] = (float)SceneHeight(); cb[18] = widthPx * (float)SceneWidth() / (float)FrameWidth(); cb[19] = 0.f;
	}
	// [rc4l] Over the player's view, tested against its depth as GL draws them at the end of its translucent pass.
	Diligent::ITextureView *rtv = SceneRTV();
	Diligent::ITextureView *dsv = EnsureSceneDepth();
	if (!dsv) dsv = FrameDSV();
	if (!rtv) return;
	ctx->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	{
		Diligent::Viewport vp;
		vp.Width = (float)SceneWidth(); vp.Height = (float)SceneHeight();
		vp.MinDepth = 0.f; vp.MaxDepth = 1.f;
		ctx->SetViewports(1, &vp, (Diligent::Uint32)FrameWidth(), (Diligent::Uint32)FrameHeight());
	}
	Diligent::IBuffer *vbs[] = { g_lineVB };
	const Diligent::Uint64 offsets[] = { 0 };
	ctx->SetVertexBuffers(0, 1, vbs, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
	const int which = xray ? 1 : 0;
	ctx->SetPipelineState(g_linePSO[which]);
	ctx->CommitShaderResources(g_lineSRB[which], Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	Diligent::DrawAttribs d;
	d.NumVertices = verts.Size();
	d.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
	ctx->Draw(d);
}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
