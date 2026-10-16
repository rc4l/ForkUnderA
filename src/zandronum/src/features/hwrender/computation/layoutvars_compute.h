// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A pipeline's variable list with every storage buffer its shaders declare flagged, without Diligent's types.

#ifndef ZX_LAYOUTVARS_COMPUTE_H
#define ZX_LAYOUTVARS_COMPUTE_H

#include <string>
#include <vector>

namespace zx { namespace hwrender {

// [rc4l] One entry of a pipeline's variable list: the stages it covers as Diligent's SHADER_TYPE bits, its name, its variable type and its flags.
struct LayoutVar
{
	unsigned stages;
	std::string name;
	int type;
	unsigned flags;
};

// [rc4l] A storage buffer one shader stage declares.
struct StageBuffer
{
	unsigned stage;
	std::string name;
};

// [rc4l] `flag` on every listed variable that names one of the buffers in its stage, and a new entry of `defaultType` with it for each buffer the list leaves out.
std::vector<LayoutVar> ComputeFlagStorageBuffers(const std::vector<LayoutVar> &vars, const std::vector<StageBuffer> &buffers,
	int defaultType, unsigned flag);

}} // namespace zx::hwrender

#endif // ZX_LAYOUTVARS_COMPUTE_H
