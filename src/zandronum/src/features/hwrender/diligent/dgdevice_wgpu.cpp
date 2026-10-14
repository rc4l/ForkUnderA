// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's device and swapchain on WebGPU: through Dawn on the desktop, and in a browser the device the page made before the engine started.

#include "features/hwrender/diligent/dgdevice.h"
#include "features/hwrender/backendonly.h"   // [rc4l] CaptureFrameLater
#include "zstring.h"

#ifdef FUA_WEBGPU

#include "EngineFactoryWebGPU.h"
#if PLATFORM_WEB
#include <emscripten.h>
#include <emscripten/html5_webgpu.h>
#endif

namespace zx { namespace hwrender {

static bool CreateDevice(Diligent::IRenderDevice **device, Diligent::IDeviceContext **context, DescriptorLimits &limits, FString &report)
{
	auto *factory = Diligent::GetEngineFactoryWebGPU();
	if (factory == nullptr)
	{
		report = "GetEngineFactoryWebGPU returned null";
		return false;
	}
	Diligent::EngineWebGPUCreateInfo ci;
	// [rc4l] Depth clamp is WebGPU's optional depth-clip-control, which Diligent asks the adapter for under DepthBiasClamp; the rest of what Vulkan is asked for, WebGPU has none of.
	ci.Features.DepthClamp = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.DepthBiasClamp = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
#if PLATFORM_WEB
	// [rc4l] A browser makes its device asynchronously, so the page makes it first (Module.preinitializedWebGPUDevice) and the backend attaches to it.
	WGPUDevice pageDevice = emscripten_webgpu_get_device();
	if (pageDevice == nullptr)
	{
		report = "the page made no WebGPU device";
		return false;
	}
	factory->AttachToWebGPUDevice(wgpuCreateInstance(nullptr), nullptr, pageDevice, ci, device, context);
#else
	factory->CreateDeviceAndContextsWebGPU(ci, device, context);
#endif
	if (*device == nullptr)
	{
		report = "no usable WebGPU device";
		return false;
	}
	// [rc4l] No texture arrays in WebGPU, so no material array: a binding per material.
	limits = DescriptorLimits{ 0, 0, 0, 0, 0 };
	return true;
}

static void CreateSwapChain(Diligent::IRenderDevice *device, Diligent::IDeviceContext *context, const Diligent::SwapChainDesc &desc,
	const Diligent::NativeWindow &window, Diligent::ISwapChain **swap)
{
	Diligent::GetEngineFactoryWebGPU()->CreateSwapChainWebGPU(device, context, desc, window, swap);
}

#if PLATFORM_WEB
// [rc4l] The canvas holds the frame only until the page's task ends, so the page copies it after the frame (Module.postMainLoop) rather than now.
bool CaptureFrameLater(const char *path)
{
	return EM_ASM_INT({
		if (!Module.fuaCaptureFrame) return 0;
		Module.fuaCaptureFrame(UTF8ToString($0));
		return 1;
	}, path) != 0;
}
#else
bool CaptureFrameLater(const char *) { return false; }
#endif

const DeviceApi *WebGPUDeviceApi()
{
#if PLATFORM_WEB
	// [rc4l] A browser maps buffers only asynchronously, and paces the page's frames itself.
	const bool inBrowser = true;
#else
	const bool inBrowser = false;
#endif
	static const DeviceApi api = { "WebGPU", CreateDevice, CreateSwapChain, !inBrowser, inBrowser };
	return &api;
}

}} // namespace zx::hwrender

#else

namespace zx { namespace hwrender {
const DeviceApi *WebGPUDeviceApi() { return nullptr; }
bool CaptureFrameLater(const char *) { return false; }
}} // namespace zx::hwrender

#endif // FUA_WEBGPU
