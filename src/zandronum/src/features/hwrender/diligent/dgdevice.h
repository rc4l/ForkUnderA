// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's device and swapchain made on one graphics API: Vulkan (dgdevice_vk.cpp) or WebGPU (dgdevice_wgpu.cpp), each NULL in a build without it.

#ifndef ZX_DGDEVICE_H
#define ZX_DGDEVICE_H

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "NativeWindow.h"
#include "features/hwrender/computation/materialslots_compute.h"   // [rc4l] DescriptorLimits

class FString;

namespace zx { namespace hwrender {

struct DeviceApi
{
	// [rc4l] The API's name for the console, as in "Vulkan device created".
	const char *name;
	// [rc4l] The device and its context with the backend's optional features asked for, and the descriptor limits the material array is sized from (zero where there is no array); false with the reason in report.
	bool (*createDevice)(Diligent::IRenderDevice **device, Diligent::IDeviceContext **context, DescriptorLimits &limits, FString &report);
	// [rc4l] A swapchain on the window.
	void (*createSwapChain)(Diligent::IRenderDevice *device, Diligent::IDeviceContext *context, const Diligent::SwapChainDesc &desc,
		const Diligent::NativeWindow &window, Diligent::ISwapChain **swap);
	// [rc4l] Whether the CPU can wait for the GPU (an idle wait, a readback mapped at once), which a browser's WebGPU only does asynchronously.
	bool canWaitForGPU;
	// [rc4l] Whether a page's compositor paces the frames (a browser's canvas), which ComputePresentInterval presents at 1.
	bool pagePaced;
	// [rc4l] Keeps a texture or buffer so freeReleased can free its GPU memory once nothing holds it; NULL where releasing it frees it, as on Vulkan.
	void (*keep)(Diligent::IDeviceObject *object);
	// [rc4l] Frees each kept object nothing holds any more, once a present has submitted the frame that last drew with it.
	void (*freeReleased)();
};

const DeviceApi *VulkanDeviceApi();
const DeviceApi *WebGPUDeviceApi();

// [rc4l] The API this launch's device is made on (ComputeBackendApi over vid_backend, -vulkan and -webgpu), from those the build has.
const DeviceApi *ChosenDeviceApi();

// [rc4l] ChosenDeviceApi's canWaitForGPU, for the readbacks and idle waits that need it.
bool CanWaitForGPU();

// [rc4l] ChosenDeviceApi's pagePaced, for the present interval.
bool PagePaced();

// [rc4l] A texture or buffer made on the device and kept by its API (DeviceApi::keep); NULL in out when it could not be made.
void CreateDeviceTexture(const Diligent::TextureDesc &desc, const Diligent::TextureData *data, Diligent::ITexture **out);
void CreateDeviceBuffer(const Diligent::BufferDesc &desc, const Diligent::BufferData *data, Diligent::IBuffer **out);

// [rc4l] After a present: keeps the swapchain's own textures, which a resize makes again, and frees every kept object nothing holds any more.
void FreeReleasedObjects(Diligent::ISwapChain *swap);

}} // namespace zx::hwrender

#endif // ZX_DGDEVICE_H
