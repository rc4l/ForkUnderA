// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include <gtest/gtest.h>

#include "features/hwrender/computation/shadercache_compute.h"

using namespace zx::hwrender;

// [Doruk-Aksoy] One compile per stage and text; asking again finds it, and the same text at another stage is its own.
TEST(ShaderCache, ATextIsCompiledOncePerStage)
{
	ShaderCache<int> cache;
	int made = 0;
	auto compile = [&]() { return ++made; };
	EXPECT_EQ(1, ComputeCachedShader(cache, 1, "void main() {}", compile));
	EXPECT_EQ(1, ComputeCachedShader(cache, 1, "void main() {}", compile));
	EXPECT_EQ(2, ComputeCachedShader(cache, 2, "void main() {}", compile));
	EXPECT_EQ(3, ComputeCachedShader(cache, 1, "void main() { }", compile));
	EXPECT_EQ(3, made);
	EXPECT_EQ(3u, cache.compiles);
	EXPECT_EQ(1u, cache.hits);
	EXPECT_EQ(3u, cache.kept.size());
}

// [Doruk-Aksoy] A failure is kept too, so a broken source is not compiled again every time a pipeline asks for it.
TEST(ShaderCache, AFailureIsKept)
{
	ShaderCache<const char *> cache;
	int tries = 0;
	auto fail = [&]() -> const char * { tries++; return nullptr; };
	EXPECT_EQ(nullptr, ComputeCachedShader(cache, 0, "broken", fail));
	EXPECT_EQ(nullptr, ComputeCachedShader(cache, 0, "broken", fail));
	EXPECT_EQ(1, tries);
	EXPECT_EQ(1u, cache.hits);
}
