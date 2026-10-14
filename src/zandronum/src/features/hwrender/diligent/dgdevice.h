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
};

const DeviceApi *VulkanDeviceApi();
const DeviceApi *WebGPUDeviceApi();

// [rc4l] The API this launch's device is made on (ComputeBackendApi over vid_backend, -vulkan and -webgpu), from those the build has.
const DeviceApi *ChosenDeviceApi();

}} // namespace zx::hwrender

#endif // ZX_DGDEVICE_H
