// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/backendchoice_compute.h"

namespace zx { namespace hwrender {

bool ComputeWantsBackendOnly(int vidBackend, bool argVulkan, bool argGL, bool built)
{
	if (!built || argGL) return false;
	return argVulkan || vidBackend != kVidBackendGL;
}

BackendApi ComputeBackendApi(int vidBackend, bool argVulkan, bool argWebGPU, bool vulkanBuilt, bool webgpuBuilt)
{
	if (!webgpuBuilt) return kBackendApiVulkan;
	if (!vulkanBuilt) return kBackendApiWebGPU;
	if (argVulkan) return kBackendApiVulkan;
	return (argWebGPU || vidBackend == kVidBackendWebGPU) ? kBackendApiWebGPU : kBackendApiVulkan;
}

int ComputeVidBackendFor(int wanted, bool vulkanBuilt, bool webgpuBuilt)
{
	if (!ComputeVidBackendChoosable(vulkanBuilt, webgpuBuilt)) return kVidBackendWebGPU;
	if (wanted == kVidBackendWebGPU && !webgpuBuilt) return kVidBackendVulkan;
	return wanted;
}

bool ComputeVidBackendChoosable(bool vulkanBuilt, bool webgpuBuilt)
{
	return vulkanBuilt || !webgpuBuilt;
}

}} // namespace zx::hwrender
