// [rc4l] Diligent's WebGPU backend under Emscripten attaches to the page's device and draws the engine's GLSL triangle.
#include <cstdio>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>

#include "EngineFactoryWebGPU.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

using namespace Diligent;

static RefCntAutoPtr<IRenderDevice>        g_dev;
static RefCntAutoPtr<IDeviceContext>       g_ctx;
static RefCntAutoPtr<ISwapChain>           g_swap;
static RefCntAutoPtr<IPipelineState>       g_pso;
static RefCntAutoPtr<IShaderResourceBinding> g_srb;
static int g_frame;

static const char *kVS =
	"#version 450\n"
	"layout(location = 0) out vec3 vColor;\n"
	"void main() {\n"
	"    vec2 p[3] = vec2[3](vec2(-0.6, -0.5), vec2(0.6, -0.5), vec2(0.0, 0.6));\n"
	"    vec3 c[3] = vec3[3](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));\n"
	"    gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);\n"
	"    vColor = c[gl_VertexIndex];\n"
	"}\n";

static const char *kPS =
	"#version 450\n"
	"layout(location = 0) in vec3 vColor;\n"
	"layout(location = 0) out vec4 outColor;\n"
	"void main() { outColor = vec4(vColor, 1.0); }\n";

static bool MakePipeline()
{
	RefCntAutoPtr<IShader> vs, ps;
	ShaderCreateInfo ci;
	ci.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
	ci.Desc.UseCombinedTextureSamplers = true;
	ci.Desc.ShaderType = SHADER_TYPE_VERTEX;
	ci.Desc.Name = "tri VS";
	ci.Source = kVS;
	g_dev->CreateShader(ci, &vs);
	ci.Desc.ShaderType = SHADER_TYPE_PIXEL;
	ci.Desc.Name = "tri PS";
	ci.Source = kPS;
	g_dev->CreateShader(ci, &ps);
	if (!vs || !ps) { printf("webgpu-check: shader compile failed\n"); return false; }
	GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "tri PSO";
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = g_swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = g_swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.pVS = vs;
	pci.pPS = ps;
	g_dev->CreateGraphicsPipelineState(pci, &g_pso);
	if (!g_pso) { printf("webgpu-check: pipeline failed\n"); return false; }
	g_pso->CreateShaderResourceBinding(&g_srb, true);
	return true;
}

static void Frame()
{
	ITextureView *rtv = g_swap->GetCurrentBackBufferRTV();
	ITextureView *dsv = g_swap->GetDepthBufferDSV();
	g_ctx->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	const float t = (g_frame % 240) / 240.f;
	const float c[4] = { t, 0.25f, 1.f - t, 1.f };
	g_ctx->ClearRenderTarget(rtv, c, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	if (g_pso)
	{
		g_ctx->SetPipelineState(g_pso);
		g_ctx->CommitShaderResources(g_srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		DrawAttribs da;
		da.NumVertices = 3;
		g_ctx->Draw(da);
	}
	g_ctx->Flush();
	g_swap->Present();
	if (++g_frame == 3) printf("webgpu-check: presented 3 frames\n");
}

int main()
{
	WGPUDevice wdev = emscripten_webgpu_get_device();
	if (wdev == nullptr) { printf("webgpu-check: no preinitialized WebGPU device\n"); return 1; }
	IEngineFactoryWebGPU *f = GetEngineFactoryWebGPU();
	EngineWebGPUCreateInfo eci;
	WGPUInstance inst = wgpuCreateInstance(nullptr);
	f->AttachToWebGPUDevice(inst, nullptr, wdev, eci, &g_dev, &g_ctx);
	if (!g_dev) { printf("webgpu-check: AttachToWebGPUDevice failed\n"); return 1; }
	SwapChainDesc sd;
	sd.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
	NativeWindow w;
	w.pCanvasId = "#canvas";
	f->CreateSwapChainWebGPU(g_dev, g_ctx, sd, w, &g_swap);
	if (!g_swap) { printf("webgpu-check: swapchain failed\n"); return 1; }
	printf("webgpu-check: device %s, swapchain %ux%u\n", g_dev->GetAdapterInfo().Description,
		g_swap->GetDesc().Width, g_swap->GetDesc().Height);
	MakePipeline();
	emscripten_set_main_loop(Frame, 0, false);
	return 0;
}
