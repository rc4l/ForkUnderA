// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's device and swapchain on WebGPU: through Dawn on the desktop, and in a browser the device the page made before the engine started.

#include "features/hwrender/diligent/dgdevice.h"
#include "zstring.h"

#ifdef FUA_WEBGPU

#include <webgpu/webgpu.h>
#include "EngineFactoryWebGPU.h"
#include "TextureWebGPU.h"
#include "BufferWebGPU.h"
#include "RefCntAutoPtr.hpp"
#include "features/hwrender/computation/keptobjects_compute.h"
#if PLATFORM_WEB
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
// [rc4l] Emscripten's own WebGPU headers name AddRef Reference.
static void HoldTexture(WGPUTexture t) { wgpuTextureReference(t); }
static void HoldBuffer(WGPUBuffer b) { wgpuBufferReference(b); }
#else
static void HoldTexture(WGPUTexture t) { wgpuTextureAddRef(t); }
static void HoldBuffer(WGPUBuffer b) { wgpuBufferAddRef(b); }
#endif

// [rc4l] Each texture and buffer the backend made, with a weak hold on the object that owns it; a browser frees a released one only when its garbage collector gets to it, which a page resized a few times outran until it was out of GPU memory.
static KeptObjects<Diligent::RefCntWeakPtr<Diligent::IDeviceObject>> g_kept;

static void Keep(Diligent::IDeviceObject *object)
{
	if (object == nullptr) return;
	const Diligent::RefCntWeakPtr<Diligent::IDeviceObject> owner(object);
	Diligent::RefCntAutoPtr<Diligent::ITextureWebGPU> texture(object, Diligent::IID_TextureWebGPU);
	if (texture)
	{
		WGPUTexture handle = texture->GetWebGPUTexture();
		if (g_kept.Keep(handle, true, owner)) HoldTexture(handle);
		return;
	}
	Diligent::RefCntAutoPtr<Diligent::IBufferWebGPU> buffer(object, Diligent::IID_BufferWebGPU);
	if (!buffer) return;
	const Diligent::USAGE usage = buffer->GetDesc().Usage;
	if (!ComputeBufferOwnsMemory(usage == Diligent::USAGE_DYNAMIC, usage == Diligent::USAGE_STAGING)) return;
	WGPUBuffer handle = buffer->GetWebGPUBuffer();
	if (g_kept.Keep(handle, false, owner)) HoldBuffer(handle);
}

// [rc4l] A present has submitted everything drawn with an object let go of before it, which WebGPU lets finish before destroying it.
static void FreeReleased()
{
	g_kept.FreeReleased(
		[](const Diligent::RefCntWeakPtr<Diligent::IDeviceObject> &owner) { return !owner.IsValid(); },
		[](void *handle, bool texture)
		{
			if (texture)
			{
				wgpuTextureDestroy((WGPUTexture)handle);
				wgpuTextureRelease((WGPUTexture)handle);
			}
			else
			{
				wgpuBufferDestroy((WGPUBuffer)handle);
				wgpuBufferRelease((WGPUBuffer)handle);
			}
		});
}

const DeviceApi *WebGPUDeviceApi()
{
#if PLATFORM_WEB
	// [rc4l] A browser maps buffers only asynchronously, and paces the page's frames itself.
	const bool inBrowser = true;
#else
	const bool inBrowser = false;
#endif
	static const DeviceApi api = { "WebGPU", CreateDevice, CreateSwapChain, !inBrowser, inBrowser, Keep, FreeReleased };
	return &api;
}

}} // namespace zx::hwrender

#else

namespace zx { namespace hwrender {
const DeviceApi *WebGPUDeviceApi() { return nullptr; }
}} // namespace zx::hwrender

#endif // FUA_WEBGPU
