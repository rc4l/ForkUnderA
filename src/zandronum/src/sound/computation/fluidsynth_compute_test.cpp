// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "sound/computation/fluidsynth_compute.h"

using zx::ComputeFluidSynthLibraries;
using zx::ComputeFluidSynthLoadFailure;
using zx::FluidSynthPlatform;

TEST(FluidSynthLibraries, NewestFirstThenEachPlatformsOldNames)
{
	EXPECT_EQ(ComputeFluidSynthLibraries(FluidSynthPlatform::Windows64),
		(std::vector<std::string>{ "libfluidsynth-3.dll", "libfluidsynth-2.dll", "fluidsynth64.dll", "libfluidsynth64.dll" }));
	EXPECT_EQ(ComputeFluidSynthLibraries(FluidSynthPlatform::Windows32),
		(std::vector<std::string>{ "libfluidsynth-3.dll", "libfluidsynth-2.dll", "fluidsynth.dll", "libfluidsynth.dll" }));
	EXPECT_EQ(ComputeFluidSynthLibraries(FluidSynthPlatform::Posix),
		(std::vector<std::string>{ "libfluidsynth.so.3", "libfluidsynth.so.2", "libfluidsynth.so.1" }));
}

TEST(FluidSynthLibraries, TheFailureLineNamesEveryLibraryTried)
{
	EXPECT_EQ(ComputeFluidSynthLoadFailure({}), "");
	EXPECT_EQ(ComputeFluidSynthLoadFailure({ "a" }), "a");
	EXPECT_EQ(ComputeFluidSynthLoadFailure({ "a", "b" }), "a or b");
	EXPECT_EQ(ComputeFluidSynthLoadFailure({ "a", "b", "c" }), "a, b, or c");
	EXPECT_EQ(ComputeFluidSynthLoadFailure(ComputeFluidSynthLibraries(FluidSynthPlatform::Windows64)),
		"libfluidsynth-3.dll, libfluidsynth-2.dll, fluidsynth64.dll, or libfluidsynth64.dll");
}
