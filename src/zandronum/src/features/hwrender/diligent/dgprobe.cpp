// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Milestone 1 of the renderer backend swap: prove DiligentCore links into ForkUnderA and can
// create a Vulkan device in-process, alongside the live GL context.
//
// This deliberately does nothing else. No swapchain, no window, no drawing. The point is to answer
// the three questions that would otherwise be discovered late and expensively:
//   1. does Diligent's CMake and its ThirdParty tree (glslang, SPIRV-Tools, volk) link against a
//      2012-era ZDoom fork without symbol or CRT conflicts,
//   2. does a Vulkan device create on this machine at all, and what does it report,
//   3. does creating it disturb the running OpenGL renderer (it must not -- the two APIs share
//      nothing but the process until a swapchain enters the picture).
//
// Compiled only when FUA_DILIGENT is on; the whole file is empty otherwise, so a normal build is
// byte-identical and carries no Apache-2.0 dependency.

#include "c_dispatch.h"
#include "c_cvars.h"
#include "c_console.h"
#include "v_text.h"   // [rc4l] TEXTCOLOR_ORANGE

#ifdef FUA_DILIGENT

// [rc4l] Diligent's platform macro (PLATFORM_WIN32, _LINUX or _MACOS) comes from the build, as its own targets have it.
#include "RefCntAutoPtr.hpp"
#include "DebugOutput.h"
#include "m_argv.h"
#include "features/hwrender/computation/backendchoice_compute.h"
#include "features/hwrender/backendonly.h"   // [rc4l] PrintDiligentMessages
#include "features/handoff/seam.h"   // [rc4l] OnMainThread
#include "features/handoff/computation/queue_compute.h"
#include <string>
#include "features/hwrender/diligent/dgdevice.h"
#include "features/hwrender/diligent/dgmemory.h"   // [rc4l] every texture and buffer counted by tag

EXTERN_CVAR(Int, vid_backend)

namespace zx { namespace hwrender {

// [rc4l] Held for the process lifetime once created, so a second probe reports the existing device
// rather than churning one. A real backend would own these; for now they only prove creation works.
static Diligent::RefCntAutoPtr<Diligent::IRenderDevice>  g_device;
static Diligent::RefCntAutoPtr<Diligent::IDeviceContext> g_context;
// [rc4l] The device's descriptor limits, read once it exists; all zero (no bindless) before that or without the feature.
static DescriptorLimits g_limits = { 0, 0, 0, 0, 0 };

}} // namespace zx::hwrender

// [rc4l] A smaller device's limits, for drawing as one would (0 is the device's own); the pipelines are rebuilt with the new array size.
CVAR(Int, fua_dg_slotcap, 0, 0)

namespace zx { namespace hwrender {

unsigned MaterialSlotCapacity()
{
	DescriptorLimits l = g_limits;
	const unsigned cap = fua_dg_slotcap > 0 ? (unsigned)fua_dg_slotcap : 0;
	if (cap > 0)
		for (unsigned *v : { &l.perStageSampledImages, &l.perStageSamplers, &l.perStageResources, &l.setSampledImages, &l.setSamplers })
			if (*v > cap) *v = cap;
	return ComputeMaterialSlotCapacity(l);
}

const DeviceApi *ChosenDeviceApi()
{
	const DeviceApi *vulkan = VulkanDeviceApi(), *webgpu = WebGPUDeviceApi();
	const BackendApi api = ComputeBackendApi(vid_backend, Args->CheckParm("-vulkan") != 0, Args->CheckParm("-webgpu") != 0,
		vulkan != nullptr, webgpu != nullptr);
	return api == kBackendApiWebGPU ? webgpu : vulkan;
}

// [rc4l] Route Diligent's own diagnostics to the engine console. Without this a shader that fails to
// compile reports only "failed" and the actual glslang error is invisible -- which cost a build/run
// cycle the first time it happened.
// [rc4l] Diligent's INFO messages are off by default, because they are not for us.
//
// They report normal driver bookkeeping -- swapchain present mode, memory pages, and a full dump of
// every Vulkan extension the device supports -- and they drown the engine console. Warnings and
// errors always come through: a shader that fails to compile must not be silent, which is the whole
// reason this callback exists.
CVAR(Bool, fua_dg_verbose, false, 0)

// [rc4l] Messages from Diligent's own threads (its thread pool names each worker through this callback), kept for the main thread, whose console it is.
static zx::Queue<std::string> &DiligentMessages()
{
	static zx::Queue<std::string> queue("hwrender/diligent_messages");
	return queue;
}

static void DiligentDebugMessage(Diligent::DEBUG_MESSAGE_SEVERITY sev, const Diligent::Char *msg,
                                 const Diligent::Char *func, const Diligent::Char *file, int line)
{
	if (sev == Diligent::DEBUG_MESSAGE_SEVERITY_INFO && !fua_dg_verbose) return;
	const char *tag = (sev == Diligent::DEBUG_MESSAGE_SEVERITY_ERROR ||
	                   sev == Diligent::DEBUG_MESSAGE_SEVERITY_FATAL_ERROR) ? "ERROR" :
	                  (sev == Diligent::DEBUG_MESSAGE_SEVERITY_WARNING) ? "warn" : "info";
	const std::string text = std::string("[Diligent ") + tag + "] " + (msg ? msg : "(null)") + "\n";
	if (!zx::handoff::OnMainThread())
	{
		DiligentMessages().Push(text);
		return;
	}
	zx::hwrender::PrintDiligentMessages();
	Printf("%s", text.c_str());
}

void PrintDiligentMessages()
{
	DiligentMessages().Drain([](const std::string &text) { Printf("%s", text.c_str()); });
}

static const char *AdapterTypeName(Diligent::ADAPTER_TYPE t)
{
	switch (t)
	{
	case Diligent::ADAPTER_TYPE_DISCRETE:   return "discrete";
	case Diligent::ADAPTER_TYPE_INTEGRATED: return "integrated";
	case Diligent::ADAPTER_TYPE_SOFTWARE:   return "software";
	default:                                return "unknown";
	}
}

// [rc4l] Accessors so the swapchain/pipeline code in dgwindow.cpp shares this one device rather than
// creating a second one -- two Vulkan devices in a process is legal and wasteful.
Diligent::IRenderDevice  *GetDevice()  { return g_device; }
Diligent::IDeviceContext *GetContext() { return g_context; }

void ClampDepthLikeGL(Diligent::GraphicsPipelineDesc &gp)
{
	gp.RasterizerDesc.DepthClipEnable = !(g_device.RawPtr() != nullptr && g_device->GetDeviceInfo().Features.DepthClamp == Diligent::DEVICE_FEATURE_STATE_ENABLED);
}

bool ProbeVulkan(FString &report)
{
	if (g_device)
	{
		// [rc4l] Report on the device that EXISTS rather than refusing.
		//
		// The features line is printed once, when the device is created, which is during startup --
		// before anything can connect to read it. So the one question this command exists to answer
		// ("does this GPU do bindless?") was the one it could not answer, and re-probing cannot help:
		// the device is already up and creating a second one is not what is being asked.
		{
			const auto &f = g_device->GetDeviceInfo( ).Features;
			const auto &rt = g_device->GetAdapterInfo( ).RayTracing;
			const auto &ad = g_device->GetAdapterInfo( );
			report.Format( "Diligent %s device already created\n"
				"  adapter: %s\n"
				"  ray tracing: %s (max recursion %u, caps 0x%x)\n"
				"  bindless resources: %s\n"
				"  shader resource static arrays: %s\n"
				"  shader resource runtime arrays: %s\n"
				"  material slots: %u",
				ChosenDeviceApi()->name, ad.Description,
				f.RayTracing ? "AVAILABLE" : "unavailable",
				(unsigned)rt.MaxRecursionDepth, (unsigned)rt.CapFlags,
				f.BindlessResources ? "yes" : "NO",
				f.ShaderResourceStaticArrays ? "yes" : "NO",
				f.ShaderResourceRuntimeArrays ? "yes" : "NO", MaterialSlotCapacity() );
		}
		return true;
	}

	Diligent::SetDebugMessageCallback(DiligentDebugMessage);

	const DeviceApi *api = ChosenDeviceApi();
	if (!api->createDevice(&g_device, &g_context, g_limits, report)) return false;
	if (MaterialSlotCapacity() > 0)
		Printf("Diligent: %u material slots (stage limits %u images, %u samplers)\n", MaterialSlotCapacity(),
			g_limits.perStageSampledImages, g_limits.perStageSamplers);

	// [rc4l] Say whether inline ray tracing is actually available, and at what capability. Building a
	// reflection path on an assumption about the device is how a backend ends up failing on someone
	// else's machine with no clue why.
	{
		const auto &f = g_device->GetDeviceInfo().Features;
		const auto &rt = g_device->GetAdapterInfo().RayTracing;
		Printf("Diligent: ray tracing %s (max recursion %u, caps 0x%x), bindless %s, sampler arrays %s, depth clamp %s\n",
			f.RayTracing ? "AVAILABLE" : "unavailable",
			(unsigned)rt.MaxRecursionDepth, (unsigned)rt.CapFlags,
			f.BindlessResources ? "yes" : "no",
			f.ShaderResourceStaticArrays ? "yes" : "no",
			f.DepthClamp ? "yes" : "no");
	}

	const auto &info = g_device->GetDeviceInfo();
	const auto &adapter = g_device->GetAdapterInfo();
	report.Format("Diligent %s device created\n"
		"  adapter: %s (%s)\n"
		"  API version: %d.%d\n"
		"  device memory: %llu MB",
		api->name, adapter.Description, AdapterTypeName(adapter.Type),
		(int)info.APIVersion.Major, (int)info.APIVersion.Minor,
		(unsigned long long)(adapter.Memory.LocalMemory / (1024 * 1024)));
	return true;
}

const char *BackendApiName() { return ChosenDeviceApi()->name; }

bool CanWaitForGPU() { return ChosenDeviceApi()->canWaitForGPU; }

bool PagePaced() { return ChosenDeviceApi()->pagePaced; }

void CreateDeviceTexture(const Diligent::TextureDesc &desc, const Diligent::TextureData *data, Diligent::ITexture **out)
{
	g_device->CreateTexture(desc, data, out);
	if (ChosenDeviceApi()->keep != nullptr) ChosenDeviceApi()->keep(*out);
	TrackDeviceTexture(*out, desc);
}

void CreateDeviceBuffer(const Diligent::BufferDesc &desc, const Diligent::BufferData *data, Diligent::IBuffer **out)
{
	g_device->CreateBuffer(desc, data, out);
	if (ChosenDeviceApi()->keep != nullptr) ChosenDeviceApi()->keep(*out);
	TrackDeviceBuffer(*out, desc);
}

void FreeReleasedObjects(Diligent::ISwapChain *swap)
{
	const DeviceApi *api = ChosenDeviceApi();
	if (api->keep == nullptr) return;
	if (swap != nullptr)
	{
		if (Diligent::ITextureView *rtv = swap->GetCurrentBackBufferRTV()) api->keep(rtv->GetTexture());
		if (Diligent::ITextureView *dsv = swap->GetDepthBufferDSV()) api->keep(dsv->GetTexture());
	}
	api->freeReleased();
}

bool BackendDeviceStarts()
{
	FString report;
	if (ProbeVulkan(report)) return true;
	Printf(TEXTCOLOR_ORANGE "%s could not start (%s); drawing with GL (Legacy) instead.\n", ChosenDeviceApi()->name, report.GetChars());
	return false;
}

}} // namespace zx::hwrender

CCMD( fua_diligent_probe )
{
	FString report;
	const bool ok = zx::hwrender::ProbeVulkan( report );
	Printf( "%s%s\n", ok ? "" : "FAILED: ", report.GetChars( ) );
}

#else // !FUA_DILIGENT

CCMD( fua_diligent_probe )
{
	Printf( "This build has no Diligent backend (configure with -DFUA_DILIGENT=ON).\n" );
}

#endif // FUA_DILIGENT
