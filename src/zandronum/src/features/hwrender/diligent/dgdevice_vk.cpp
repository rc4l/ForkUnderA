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
#include "features/telemetry/telemetry.h"

#include "c_cvars.h"

#include <algorithm>
#include <cstring>
#include <vector>

// [Doruk-Aksoy] On at launch, the Vulkan driver's own memory goes through the engine's operator new, counted and traced as the rest is (fua_allocsites); read once, when the device is made.
CVAR(Bool, fua_dg_vk_alloc_trace, false, 0)

namespace zx { namespace hwrender {

namespace {

void *VKAPI_PTR DriverAlloc(void *, size_t size, size_t alignment, VkSystemAllocationScope) { return AllocateAlignedNew(size, alignment); }
void VKAPI_PTR DriverFree(void *, void *memory) { FreeAlignedNew(memory); }

void *VKAPI_PTR DriverRealloc(void *, void *original, size_t size, size_t alignment, VkSystemAllocationScope)
{
	if (original == nullptr) return AllocateAlignedNew(size, alignment);
	if (size == 0) { FreeAlignedNew(original); return nullptr; }
	void *moved = AllocateAlignedNew(size, alignment);
	if (moved == nullptr) return nullptr;
	memcpy(moved, original, std::min(size, AlignedNewSize(original)));
	FreeAlignedNew(original);
	return moved;
}

VkAllocationCallbacks g_driverAllocator = { nullptr, DriverAlloc, DriverRealloc, DriverFree, nullptr, nullptr };

} // namespace

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

// [rc4l] The device the memory budget is read from, which lives for the process once made.
static Diligent::IRenderDevice *g_budgetDevice = nullptr;

// [rc4l] The driver's own account of each heap, everything the process allocated there (Diligent's and the driver's own included) and what it may, through VK_EXT_memory_budget; none where the device lacks it.
static void ReadHeaps(std::vector<zx::telemetry::HeapBudget> &out)
{
	Diligent::RefCntAutoPtr<Diligent::IRenderDeviceVk> vk(g_budgetDevice, Diligent::IID_RenderDeviceVk);
	if (!vk || vkGetPhysicalDeviceMemoryProperties2 == NULL || vkEnumerateDeviceExtensionProperties == NULL) return;
	const VkPhysicalDevice phys = vk->GetVkPhysicalDevice();
	static int supported = -1;
	if (supported < 0)
	{
		uint32_t n = 0;
		vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, nullptr);
		std::vector<VkExtensionProperties> exts(n);
		if (n > 0) vkEnumerateDeviceExtensionProperties(phys, nullptr, &n, exts.data());
		supported = 0;
		for (const VkExtensionProperties &e : exts) if (!std::strcmp(e.extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)) supported = 1;
	}
	if (!supported) return;
	VkPhysicalDeviceMemoryBudgetPropertiesEXT heaps = {};
	heaps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
	VkPhysicalDeviceMemoryProperties2 props = {};
	props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
	props.pNext = &heaps;
	vkGetPhysicalDeviceMemoryProperties2(phys, &props);
	for (uint32_t h = 0; h < props.memoryProperties.memoryHeapCount; h++)
		out.push_back(zx::telemetry::HeapBudget{ (props.memoryProperties.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0, (double)heaps.heapUsage[h], (double)heaps.heapBudget[h] });
}

// [rc4l] The driver's heaps, read at most a few times a second for every gauge, empty where the device cannot say.
static const std::vector<zx::telemetry::HeapBudget> &Heaps()
{
	static std::vector<zx::telemetry::HeapBudget> last;
	static zx::telemetry::Throttle every(400);
	if (every.Due(zx::telemetry::NowNs() / 1000000))
	{
		last.clear();
		ReadHeaps(last);
	}
	return last;
}

static double BudgetUsed() { return zx::telemetry::ComputeHeapBudget(Heaps(), true).used; }
static double BudgetAvailable() { return zx::telemetry::ComputeHeapBudget(Heaps(), true).budget; }
static double HostUsed() { return zx::telemetry::ComputeHeapBudget(Heaps(), false).used; }
static const bool s_budgetCounted = (
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/vk_budget_used_bytes", "bytes", zx::telemetry::CounterKind::Gauge, 500 }, BudgetUsed),
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/vk_budget_bytes", "bytes", zx::telemetry::CounterKind::Gauge, 500, false }, BudgetAvailable),
	zx::telemetry::Register(zx::telemetry::CounterSpec{ "gpu/vk_host_used_bytes", "bytes", zx::telemetry::CounterKind::Gauge, 500 }, HostUsed),
	true);

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
	// [rc4l] Duration queries time the backend's passes for features/telemetry (dggputimer.cpp).
	ci.Features.TimestampQueries = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	ci.Features.DurationQueries = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	// [Doruk-Aksoy] A thread pool for shaders compiled in the background, which the GLDEFS shaders' programs take at launch (CompileShaderAsync).
	ci.Features.AsyncShaderCompilation = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
	factory->SetMemoryAllocator(zx::hwrender::EngineMemoryAllocator());
	if (fua_dg_vk_alloc_trace) ci.pVkAllocator = &g_driverAllocator;
	factory->CreateDeviceAndContextsVk(ci, device, context);
	if (*device == nullptr)
	{
		report = "CreateDeviceAndContextsVk failed -- no usable Vulkan device.";
		return false;
	}
	limits = ReadLimits(*device);
	g_budgetDevice = *device;
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
