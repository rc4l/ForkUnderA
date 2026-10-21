// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The file a composed backend shader is dumped to, so every one can be checked against another shading language off the engine.

#ifndef ZX_SHADERDUMP_COMPUTE_H
#define ZX_SHADERDUMP_COMPUTE_H

#include <string>

namespace zx { namespace hwrender {

// [rc4l] A shader's name as a file name, letters and digits kept and every other run one underscore, with glslang's stage extension.
std::string ComputeShaderDumpName(const char *name, bool pixel);

}} // namespace zx::hwrender

#endif // ZX_SHADERDUMP_COMPUTE_H
