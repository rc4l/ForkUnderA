// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Milestones 2 and 3 of the backend swap: a Diligent swapchain that presents, and a triangle
// through a real pipeline.
//
// It runs on its OWN window, deliberately. The engine's window already carries an OpenGL context,
// and a window surface cannot serve both APIs -- so taking over the engine's window is a later
// milestone that has to remove the GL context first. Proving present + shader compilation + pipeline
// state on a separate window derisks all of that without touching the running renderer.
//
// What this establishes, none of which was previously known on this codebase:
//   * a Vulkan swapchain can be created and presented from inside the engine process,
//   * Diligent's shader factory compiles GLSL to SPIR-V here (no external glslangValidator step),
//   * a pipeline state object builds against the swapchain's formats,
//   * all of it coexists with the live GL renderer, frame after frame.

#include "c_dispatch.h"
#include "c_console.h"

#ifdef FUA_DILIGENT

#ifndef PLATFORM_WIN32
#define PLATFORM_WIN32 1
#endif

#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"
#include "Win32NativeWindow.h"

namespace zx { namespace hwrender {

bool ProbeVulkan(FString &report);   // dgprobe.cpp
Diligent::IRenderDevice  *GetDevice();
Diligent::IDeviceContext *GetContext();

// [rc4l] Opaque HWND. windows.h is deliberately not included here -- see dgwin32.cpp for why.
void *Fua_CreateBackendWindow(const char *title, int w, int h);
void  Fua_PumpBackendWindow(void *hwnd);

static void                                          *g_hwnd = NULL;
static int g_wantW = 0, g_wantH = 0;

// [rc4l] Set by the caller before the window exists; see EnsureWindow.
void Fua_SetBackendWindowSize(int w, int h) { g_wantW = w; g_wantH = h; }
static Diligent::RefCntAutoPtr<Diligent::ISwapChain>  g_swap;
static Diligent::RefCntAutoPtr<Diligent::IPipelineState> g_pso;
static int                                            g_frames = 0;

static bool EnsureWindow(FString &err)
{
	if (g_hwnd) return true;
	// [rc4l] Match the engine's own screen, so the 2D layer maps 1:1 and a screenshot pair is
	// directly comparable. Set by the caller, which knows about `screen`; falls back to 640x480.
	g_hwnd = Fua_CreateBackendWindow("ForkUnderA - Diligent (Vulkan) backend",
		g_wantW > 0 ? g_wantW : 640, g_wantH > 0 ? g_wantH : 480);
	if (!g_hwnd) { err = "backend window creation failed"; return false; }
	return true;
}

// [rc4l] GLSL rather than HLSL: the engine's shaders are GLSL, so proving Diligent's GLSL path works
// is what matters for a future port. Diligent runs it through glslang to SPIR-V internally.
//
// VERBATIM, not plain GLSL: the plain mode applies Diligent's own preprocessing and expects its
// conventions, while this is raw `#version 450` using gl_VertexIndex. Verbatim compiles it as-is,
// which is also how the engine's existing GLSL would have to be fed in.
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

static bool EnsurePipeline(FString &err)
{
	if (g_pso) return true;

	auto *dev = GetDevice();
	if (dev == NULL || !g_swap) { err = "no device/swapchain"; return false; }

	Diligent::RefCntAutoPtr<Diligent::IShader> vs, ps;
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.UseCombinedTextureSamplers = true;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
		ci.Desc.Name = "fua tri VS";
		ci.Source = kVS;
		dev->CreateShader(ci, &vs);
	}
	{
		Diligent::ShaderCreateInfo ci;
		ci.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
		ci.Desc.UseCombinedTextureSamplers = true;
		ci.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
		ci.Desc.Name = "fua tri PS";
		ci.Source = kPS;
		dev->CreateShader(ci, &ps);
	}
	if (!vs || !ps) { err = "shader compilation failed (GLSL -> SPIR-V)"; return false; }

	Diligent::GraphicsPipelineStateCreateInfo pci;
	pci.PSODesc.Name = "fua tri PSO";
	pci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
	pci.GraphicsPipeline.NumRenderTargets = 1;
	pci.GraphicsPipeline.RTVFormats[0] = g_swap->GetDesc().ColorBufferFormat;
	pci.GraphicsPipeline.DSVFormat = g_swap->GetDesc().DepthBufferFormat;
	pci.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	pci.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
	pci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
	pci.pVS = vs;
	pci.pPS = ps;
	dev->CreateGraphicsPipelineState(pci, &g_pso);
	if (!g_pso) { err = "pipeline state creation failed"; return false; }
	return true;
}

bool DiligentShowWindow(FString &report)
{
	FString err;
	if (!ProbeVulkan(err)) { report = err; return false; }
	if (!EnsureWindow(err)) { report = err; return false; }

	if (!g_swap)
	{
		auto *factory = Diligent::GetEngineFactoryVk();
		Diligent::SwapChainDesc scd;
		// [rc4l] Match the engine's own framebuffer: plain UNORM, not Diligent's default sRGB.
		//
		// With an sRGB target the hardware encodes the shader's output on write, so the stored bytes
		// are numerically much brighter than what was written. The GL renderer writes to a plain
		// UNORM buffer whose contents are treated as already-sRGB. Both end up looking the same on
		// screen, but their BUFFERS differ -- which made every readback comparison against GL wrong,
		// and sent a long hunt after a lighting bug that was really a screenshot bug.
		scd.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
		Diligent::Win32NativeWindow win(g_hwnd);
		factory->CreateSwapChainVk(GetDevice(), GetContext(), scd, win, &g_swap);
		if (!g_swap) { report = "CreateSwapChainVk failed"; return false; }
		// [rc4l] Say what depth format we actually got. Z-fighting the GL renderer does not have is
		// first a question about depth precision, and "Diligent probably defaults to D32" is not an
		// answer -- a 16-bit depth buffer against a 5..65536 frustum would fight everywhere.
		Printf("Diligent swapchain: color %d, depth %d (%dx%d)\n",
			(int)g_swap->GetDesc().ColorBufferFormat, (int)g_swap->GetDesc().DepthBufferFormat,
			(int)g_swap->GetDesc().Width, (int)g_swap->GetDesc().Height);
	}

	if (!EnsurePipeline(err)) { report = err; return false; }

	report.Format("Diligent swapchain %dx%d, pipeline ready. Use fua_diligent_frame to present.",
		(int)g_swap->GetDesc().Width, (int)g_swap->GetDesc().Height);
	return true;
}

bool DiligentFrame(int count, FString &report)
{
	if (!g_swap || !g_pso) { report = "call fua_diligent_window first"; return false; }
	auto *ctx = GetContext();

	for (int i = 0; i < count; i++)
	{
		auto *rtv = g_swap->GetCurrentBackBufferRTV();
		auto *dsv = g_swap->GetDepthBufferDSV();
		ctx->SetRenderTargets(1, &rtv, dsv, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		// [rc4l] A slowly cycling clear colour, so a still screenshot proves frames are actually
		// being presented rather than one frame sitting there.
		const float t = (float)((g_frames + i) % 120) / 120.0f;
		const float clear[4] = { 0.08f, 0.10f + 0.35f * t, 0.22f, 1.0f };
		ctx->ClearRenderTarget(rtv, clear, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		if (dsv) ctx->ClearDepthStencil(dsv, Diligent::CLEAR_DEPTH_FLAG, 1.0f, 0,
			Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		ctx->SetPipelineState(g_pso);
		Diligent::DrawAttribs draw;
		draw.NumVertices = 3;
		draw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
		ctx->Draw(draw);

		g_swap->Present(0);
		Fua_PumpBackendWindow(g_hwnd);
	}
	g_frames += count;

	report.Format("presented %d frame(s); %d total", count, g_frames);
	return true;
}

// [rc4l] Accessors for dgscene.cpp -- one swapchain and one window, shared.
Diligent::ISwapChain *GetSwapChain() { return g_swap; }
void *GetBackendWindow() { return g_hwnd; }

}} // namespace zx::hwrender

CCMD( fua_diligent_window )
{
	FString report;
	const bool ok = zx::hwrender::DiligentShowWindow( report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

CCMD( fua_diligent_frame )
{
	const int n = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 1;
	FString report;
	const bool ok = zx::hwrender::DiligentFrame( n, report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

#else // !FUA_DILIGENT

CCMD( fua_diligent_window ) { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }
CCMD( fua_diligent_frame )  { Printf( "This build has no Diligent backend (-DFUA_DILIGENT=ON).\n" ); }

#endif // FUA_DILIGENT
