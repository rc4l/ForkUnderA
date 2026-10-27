// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] Whether a shader's SPIR-V goes through SPIRV-Tools' optimizer, about 93% of a compile on Vulkan with no GPU time saved, since the driver optimizes it again when it makes the pipeline.

#ifndef ZX_HWRENDER_SPIRVOPTIMIZE_COMPUTE_H
#define ZX_HWRENDER_SPIRVOPTIMIZE_COMPUTE_H

namespace zx { namespace hwrender {

// [Doruk-Aksoy] fua_dg_spirv_optimize's `setting`: 1 always, 0 never, and anything else (the default) only on WebGPU, whose WGSL conversion has not been checked without it.
bool ComputeOptimizeSpirv(int setting, bool webgpu);

}} // namespace zx::hwrender

#endif // ZX_HWRENDER_SPIRVOPTIMIZE_COMPUTE_H
