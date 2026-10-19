// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>
#include "features/hwrender/computation/texfilter_compute.h"

using namespace zx::hwrender;

// [rc4l] TexFilter[] in gl_texture.cpp: none, nearest mipmapped, linear, bilinear, trilinear, nearest with linear mips.
TEST(TexFilterCompute, EachModeIsTheEnginesOwn)
{
	const bool expect[6][4] = {
		// min    mag    mip    mipmapped
		{ false, false, false, false },
		{ false, false, false, true  },
		{ true,  true,  false, false },
		{ true,  true,  false, true  },
		{ true,  true,  true,  true  },
		{ false, false, true,  true  },
	};
	for (int m = 0; m < 6; m++)
	{
		const TextureFilter f = ComputeTextureFilter(m);
		EXPECT_EQ(expect[m][0], f.minLinear) << m;
		EXPECT_EQ(expect[m][1], f.magLinear) << m;
		EXPECT_EQ(expect[m][2], f.mipLinear) << m;
		EXPECT_EQ(expect[m][3], f.mipmapped) << m;
	}
}

// [rc4l] WDI03's 8x8 test skybox: GL drew it crisp at mode 0 while the backend's sky smoothed it regardless.
TEST(TexFilterCompute, AModeOutOfRangeIsNearestWithNoMips)
{
	for (int m : { -1, 6, 99 })
	{
		const TextureFilter f = ComputeTextureFilter(m);
		EXPECT_FALSE(f.minLinear || f.magLinear || f.mipLinear || f.mipmapped) << m;
	}
}

// [rc4l] Trilinear takes the setting rounded and capped at 16; 1 and below, or a nearest or unmipped filter, take none.
TEST(TextureFilter, AnisotropyOnlyForALinearMipmappedFilter)
{
	const TextureFilter trilinear = { true, true, true, true };
	EXPECT_EQ(8u, ComputeAnisotropy(trilinear, 8.f));
	EXPECT_EQ(2u, ComputeAnisotropy(trilinear, 1.5f));
	EXPECT_EQ(16u, ComputeAnisotropy(trilinear, 32.f));
	EXPECT_EQ(0u, ComputeAnisotropy(trilinear, 1.f));
	EXPECT_EQ(0u, ComputeAnisotropy({ false, true, true, true }, 8.f));
	EXPECT_EQ(0u, ComputeAnisotropy({ true, false, true, true }, 8.f));
	EXPECT_EQ(0u, ComputeAnisotropy({ true, true, true, false }, 8.f));
}

// [rc4l] nuts.wad with Complex Doom made 78 MB of sprite textures in a browser, each with a mip chain the default nearest filter never samples.
TEST(TexFilterCompute, AMaterialHasMipsOnlyWhenTheFilterReadsThem)
{
	EXPECT_FALSE(zx::hwrender::ComputeMaterialMips(true, zx::hwrender::ComputeTextureFilter(0)));
	EXPECT_FALSE(zx::hwrender::ComputeMaterialMips(true, zx::hwrender::ComputeTextureFilter(2)));
	EXPECT_TRUE(zx::hwrender::ComputeMaterialMips(true, zx::hwrender::ComputeTextureFilter(4)));
	EXPECT_FALSE(zx::hwrender::ComputeMaterialMips(false, zx::hwrender::ComputeTextureFilter(4)));
}
