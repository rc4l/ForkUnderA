// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy

#include "fuamodel_compute.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

using namespace zx::models;

namespace {

FuaModelDefs Parse(const char *text) { return ComputeParseFuaModel(text, strlen(text)); }

}

TEST(FuaModel, FlatSurfaceNamesOneOrMoreActors)
{
	const FuaModelDefs d = Parse("FlatSurface Spell_ThreadedVolley_Ground\nflatsurface A B\r\nFLATSURFACE\tC");
	EXPECT_EQ(d.flatSurfaces, (std::vector<std::string>{ "Spell_ThreadedVolley_Ground", "A", "B", "C" }));
	EXPECT_TRUE(d.errors.empty());
}

TEST(FuaModel, CommentsAndBlankLinesAreSkipped)
{
	const FuaModelDefs d = Parse("// the soil\n\n   \nFlatSurface Soil // and its rim\nFlatSurface Rim//glued\n// FlatSurface Not");
	EXPECT_EQ(d.flatSurfaces, (std::vector<std::string>{ "Soil", "Rim" }));
	EXPECT_TRUE(d.errors.empty());
}

TEST(FuaModel, WhatItCannotReadIsNamedWithItsLineAndTheRestIsKept)
{
	const FuaModelDefs d = Parse("Flat Soil\nFlatSurface\nFlatSurface Rim\nFlatSurfaces X");
	EXPECT_EQ(d.flatSurfaces, (std::vector<std::string>{ "Rim" }));
	ASSERT_EQ(d.errors.size(), 3u);
	EXPECT_EQ(d.errors[0], "line 1: unknown keyword 'Flat'");
	EXPECT_EQ(d.errors[1], "line 2: FlatSurface names no actor");
	EXPECT_EQ(d.errors[2], "line 4: unknown keyword 'FlatSurfaces'");
}

TEST(FuaModel, AnEmptyLumpSaysNothing)
{
	const FuaModelDefs d = ComputeParseFuaModel("", 0);
	EXPECT_TRUE(d.flatSurfaces.empty());
	EXPECT_TRUE(d.errors.empty());
	EXPECT_TRUE(Parse("/").errors.size() == 1);   // [Doruk-Aksoy] a lone slash is a word, not a comment
}
