// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The backend's device and swapchain on Vulkan, with the descriptor limits read through the entry points Diligent loaded.

#include "features/hwrender/diligent/dgdevice.h"
#include "zstring.h"

#ifdef FUA_DILIGENT_VULKAN

#include "volk.h"
#include "EngineFactoryVk.h"
#include "RenderDeviceVk.h"
#include "RefCntAutoPtr.hpp"
#include "features/hwrender/diligent/dgplatform.h"   // [rc4l] Fua_PrepareVulkanLoader

namespace zx { namespace hwrender {

// [rc4l] The device's own descriptor limits, so a GPU smaller than a desktop one draws with a binding per material instead of failing pipeline creation.
static DescriptorLimits ReadLimits(Diligent::IRenderDevice *device)
{
	DescriptorLimits none = { 0, 0, 0, 0, 0 };
	Diligent::RefCntAutoPtr<Diligent::IRenderDeviceVk> vk(device, Diligent::IID_RenderDeviceVk);
	if (!vk || !device->GetDeviceInfo().Features.BindlessResources || vkGetPhysicalDeviceProperties == NULL) return none;
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(vk->GetVkPhysicalDevice(), &props);
	const VkPhysicalDeviceLimits &l = props.limits;
	DescriptorLimits got = { l.maxPerStageDescriptorSampledImages, l.maxPerStageDescriptorSamplers, l.maxPerStageResources,
		l.maxDescriptorSetSampledImages, l.maxDescriptorSetSamplers };
	return got;
}

static bool CreateDevice(Diligent::IRenderDevice **device, Diligent::IDeviceContext **context, DescriptorLimits &limits, FString &report)
{
	Fua_PrepareVulkanLoader();
	auto *factory = Diligent::GetEngineFactoryVk();
	if (factory == nullptr)
	{
		report = "GetEngineFactoryVk returned null (no Vulkan runtime?)";
		return false;
	}
	Diligent::EngineVkCreateInfo ci;
	// [rc4l] Ray tracing, the material array's features and depth clamp are each OPTIONAL, since a required feature the device lacks fails creation outright and the backend asks afterwards instead.
	ci.Features.RayTracing = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.BindlessResources = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.ShaderResourceStaticArrays = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.ShaderResourceRuntimeArrays = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.DepthClamp = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	factory->CreateDeviceAndContextsVk(ci, device, context);
	if (*device == nullptr)
	{
		report = "CreateDeviceAndContextsVk failed -- no usable Vulkan device.";
		return false;
	}
	limits = ReadLimits(*device);
	return true;
}

static void CreateSwapChain(Diligent::IRenderDevice *device, Diligent::IDeviceContext *context, const Diligent::SwapChainDesc &desc,
	const Diligent::NativeWindow &window, Diligent::ISwapChain **swap)
{
	Diligent::GetEngineFactoryVk()->CreateSwapChainVk(device, context, desc, window, swap);
}

const DeviceApi *VulkanDeviceApi()
{
	// [rc4l] Diligent frees a released Vulkan object once the GPU is done with it, so nothing is kept.
	static const DeviceApi api = { "Vulkan", CreateDevice, CreateSwapChain, true, false, nullptr, nullptr };
	return &api;
}

}} // namespace zx::hwrender

#else

namespace zx { namespace hwrender {
const DeviceApi *VulkanDeviceApi() { return nullptr; }
}} // namespace zx::hwrender

#endif // FUA_DILIGENT_VULKAN
