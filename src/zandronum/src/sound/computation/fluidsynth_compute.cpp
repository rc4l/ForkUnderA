// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "sound/computation/fluidsynth_compute.h"

namespace zx {

std::vector<std::string> ComputeFluidSynthLibraries(FluidSynthPlatform platform)
{
	switch (platform)
	{
	case FluidSynthPlatform::Windows32: return { "libfluidsynth-3.dll", "libfluidsynth-2.dll", "fluidsynth.dll", "libfluidsynth.dll" };
	case FluidSynthPlatform::Windows64: return { "libfluidsynth-3.dll", "libfluidsynth-2.dll", "fluidsynth64.dll", "libfluidsynth64.dll" };
	case FluidSynthPlatform::Posix: break;
	}
	return { "libfluidsynth.so.3", "libfluidsynth.so.2", "libfluidsynth.so.1" };
}

std::string ComputeFluidSynthLoadFailure(const std::vector<std::string> &names)
{
	std::string text;
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			text += (names.size() > 2) ? ", " : " ";
		if (i > 0 && i + 1 == names.size())
			text += "or ";
		text += names[i];
	}
	return text;
}

} // namespace zx
