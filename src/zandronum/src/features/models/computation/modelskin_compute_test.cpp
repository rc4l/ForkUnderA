// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "features/models/computation/modelskin_compute.h"

#include <gtest/gtest.h>

using zx::models::ComputeModelSurfaceSkin;

namespace {

// [Doruk-Aksoy] Three distinct addresses standing in for textures, which the helper only compares and hands back.
int g_modeldef, g_surface, g_own;
FTexture *const kModeldef = reinterpret_cast<FTexture *>(&g_modeldef);
FTexture *const kSurface = reinterpret_cast<FTexture *>(&g_surface);
FTexture *const kOwn = reinterpret_cast<FTexture *>(&g_own);

} // namespace

TEST(ModelSurfaceSkin, ModeldefSkinWinsOverEveryOther)
{
	EXPECT_EQ(kModeldef, ComputeModelSurfaceSkin(kModeldef, kSurface, kOwn));
	EXPECT_EQ(kModeldef, ComputeModelSurfaceSkin(kModeldef, nullptr, nullptr));
}

TEST(ModelSurfaceSkin, SurfaceSkinWinsOverTheFilesOwn)
{
	EXPECT_EQ(kSurface, ComputeModelSurfaceSkin(nullptr, kSurface, kOwn));
	EXPECT_EQ(kSurface, ComputeModelSurfaceSkin(nullptr, kSurface, nullptr));
}

// [Doruk-Aksoy] A MODELDEF voxel with no Skin line: its own palette, where the null skin crashed GL's draw.
TEST(ModelSurfaceSkin, FallsBackToTheFilesOwnSkin)
{
	EXPECT_EQ(kOwn, ComputeModelSurfaceSkin(nullptr, nullptr, kOwn));
}

TEST(ModelSurfaceSkin, NoSkinAnywhereIsNull)
{
	EXPECT_EQ(nullptr, ComputeModelSurfaceSkin(nullptr, nullptr, nullptr));
}
