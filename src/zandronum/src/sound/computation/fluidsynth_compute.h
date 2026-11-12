// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which FluidSynth library a dynamically loading build tries, and what it says when none loads.

#ifndef ZX_FLUIDSYNTH_COMPUTE_H
#define ZX_FLUIDSYNTH_COMPUTE_H

#include <string>
#include <vector>

namespace zx {

enum class FluidSynthPlatform { Windows32, Windows64, Posix };

// [rc4l] Adapted from zandronum@ceb98a637090235fb3948a5333969c2f23bf3e5c: the 2.2 and 2.1 libraries first, then the old names, as one list where upstream nests a fallback per name.
std::vector<std::string> ComputeFluidSynthLibraries(FluidSynthPlatform platform);

// [rc4l] The names joined for the failure line: "a", "a or b", or "a, b, or c".
std::string ComputeFluidSynthLoadFailure(const std::vector<std::string> &names);

} // namespace zx

#endif
