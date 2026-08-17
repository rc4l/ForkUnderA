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
#include "c_console.h"

#ifdef FUA_DILIGENT

// Diligent's headers want the platform macro before anything else.
#ifndef PLATFORM_WIN32
#define PLATFORM_WIN32 1
#endif

#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"
#include "DebugOutput.h"

namespace zx { namespace hwrender {

// [rc4l] Held for the process lifetime once created, so a second probe reports the existing device
// rather than churning one. A real backend would own these; for now they only prove creation works.
static Diligent::RefCntAutoPtr<Diligent::IRenderDevice>  g_device;
static Diligent::RefCntAutoPtr<Diligent::IDeviceContext> g_context;

// [rc4l] Route Diligent's own diagnostics to the engine console. Without this a shader that fails to
// compile reports only "failed" and the actual glslang error is invisible -- which cost a build/run
// cycle the first time it happened.
static void DiligentDebugMessage(Diligent::DEBUG_MESSAGE_SEVERITY sev, const Diligent::Char *msg,
                                 const Diligent::Char *func, const Diligent::Char *file, int line)
{
	const char *tag = (sev == Diligent::DEBUG_MESSAGE_SEVERITY_ERROR ||
	                   sev == Diligent::DEBUG_MESSAGE_SEVERITY_FATAL_ERROR) ? "ERROR" :
	                  (sev == Diligent::DEBUG_MESSAGE_SEVERITY_WARNING) ? "warn" : "info";
	Printf("[Diligent %s] %s\n", tag, msg ? msg : "(null)");
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

bool ProbeVulkan(FString &report)
{
	if (g_device)
	{
		report = "Diligent Vulkan device already created.";
		return true;
	}

	Diligent::SetDebugMessageCallback(DiligentDebugMessage);

	auto *factory = Diligent::GetEngineFactoryVk();
	if (factory == nullptr)
	{
		report = "GetEngineFactoryVk returned null (no Vulkan runtime?)";
		return false;
	}

	Diligent::EngineVkCreateInfo ci;
	// [rc4l] Ask for ray tracing as OPTIONAL, never as required.
	//
	// Inline ray queries are what a mirror actually wants: no visibility list, recursion for free,
	// and cost proportional to the mirror's pixels rather than a full-screen pass per mirror. But an
	// RT-capable GPU cannot be assumed, and requesting a feature the device lacks fails device
	// creation outright -- which would turn "no reflections on old hardware" into "the backend does
	// not start". OPTIONAL means the device comes up either way and the code asks afterwards.
	ci.Features.RayTracing = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	// [rc4l] And the features a traced reflection needs to pick its own texture.
	//
	// A rasterised draw binds one material and draws its batch; a ray can land on any triangle in the
	// level, so the material must be selectable from inside the shader -- which is an indexed array of
	// samplers. Both of these default to DISABLED, and using a sampler array without them killed the
	// process on launch with nothing in the log, which reads as "bindless does not work" rather than
	// "bindless was never asked for".
	ci.Features.BindlessResources = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.ShaderResourceStaticArrays = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.ShaderResourceRuntimeArrays = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	factory->CreateDeviceAndContextsVk(ci, &g_device, &g_context);
	if (!g_device)
	{
		report = "CreateDeviceAndContextsVk failed -- no usable Vulkan device.";
		return false;
	}

	// [rc4l] Say whether inline ray tracing is actually available, and at what capability. Building a
	// reflection path on an assumption about the device is how a backend ends up failing on someone
	// else's machine with no clue why.
	{
		const auto &f = g_device->GetDeviceInfo().Features;
		const auto &rt = g_device->GetAdapterInfo().RayTracing;
		Printf("Diligent: ray tracing %s (max recursion %u, caps 0x%x), bindless %s, sampler arrays %s\n",
			f.RayTracing ? "AVAILABLE" : "unavailable",
			(unsigned)rt.MaxRecursionDepth, (unsigned)rt.CapFlags,
			f.BindlessResources ? "yes" : "no",
			f.ShaderResourceStaticArrays ? "yes" : "no");
	}

	const auto &info = g_device->GetDeviceInfo();
	const auto &adapter = g_device->GetAdapterInfo();
	report.Format("Diligent Vulkan device created\n"
		"  adapter: %s (%s)\n"
		"  API version: %d.%d\n"
		"  device memory: %llu MB",
		adapter.Description, AdapterTypeName(adapter.Type),
		(int)info.APIVersion.Major, (int)info.APIVersion.Minor,
		(unsigned long long)(adapter.Memory.LocalMemory / (1024 * 1024)));
	return true;
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
