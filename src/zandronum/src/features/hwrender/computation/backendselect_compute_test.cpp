// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "features/hwrender/computation/backendselect_compute.h"

#include <gtest/gtest.h>

namespace
{

TEST(BackendSelect, ResolvesEachValidValue)
{
	EXPECT_EQ(zx::ResolveBackend(0, true), zx::RenderBackend::LegacyGL);
	EXPECT_EQ(zx::ResolveBackend(1, true), zx::RenderBackend::CoreGL);
	EXPECT_EQ(zx::ResolveBackend(2, true), zx::RenderBackend::Vulkan);
}

TEST(BackendSelect, VulkanFallsBackToCoreWhenUnsupported)
{
	EXPECT_EQ(zx::ResolveBackend(2, false), zx::RenderBackend::CoreGL);
	// Core GL never depends on Vulkan support.
	EXPECT_EQ(zx::ResolveBackend(1, false), zx::RenderBackend::CoreGL);
}

TEST(BackendSelect, OutOfRangeClampsToLegacy)
{
	EXPECT_EQ(zx::ResolveBackend(-1, true), zx::RenderBackend::LegacyGL);
	EXPECT_EQ(zx::ResolveBackend(99, true), zx::RenderBackend::LegacyGL);
	EXPECT_EQ(zx::ResolveBackend(3, false), zx::RenderBackend::LegacyGL);
}

TEST(BackendSelect, OnlyLegacyWantsCompatibilityContext)
{
	EXPECT_FALSE(zx::NeedsModernContext(zx::RenderBackend::LegacyGL));
	EXPECT_TRUE(zx::NeedsModernContext(zx::RenderBackend::CoreGL));
	EXPECT_TRUE(zx::NeedsModernContext(zx::RenderBackend::Vulkan));
}

TEST(BackendSelect, RestartNeededOnlyWhenBackendChanges)
{
	EXPECT_FALSE(zx::NeedsRestart(zx::RenderBackend::LegacyGL, zx::RenderBackend::LegacyGL));
	EXPECT_TRUE(zx::NeedsRestart(zx::RenderBackend::LegacyGL, zx::RenderBackend::CoreGL));
	EXPECT_TRUE(zx::NeedsRestart(zx::RenderBackend::CoreGL, zx::RenderBackend::Vulkan));
}

} // namespace
