// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/web/computation/crashcontext_compute.h"

#include <gtest/gtest.h>

namespace {

using zx::web::CrashContext;

TEST(CrashContext, OnlyAnOfficialBuildCarriesTheDsn)
{
	EXPECT_EQ(zx::web::ComputeCrashDsn(true, "https://key@host/1"), "https://key@host/1");
	EXPECT_EQ(zx::web::ComputeCrashDsn(false, "https://key@host/1"), "");
	EXPECT_EQ(zx::web::ComputeCrashDsn(true, nullptr), "");
}

TEST(CrashContext, FilesAreBareNamesInLoadOrder)
{
	const std::vector<std::string> names = zx::web::ComputeCrashFiles({ "/fua_core.pk3", "C:\\Users\\someone\\wads\\doom2.wad", "plain.pk3" });
	EXPECT_EQ(names, (std::vector<std::string>{ "fua_core.pk3", "doom2.wad", "plain.pk3" }));
}

TEST(CrashContext, ATagSetAgainKeepsItsPlaceWithTheNewValue)
{
	CrashContext c;
	zx::web::ComputeSetCrashTag(c, "renderer", "webgpu");
	zx::web::ComputeSetCrashTag(c, "mode", "single");
	zx::web::ComputeSetCrashTag(c, "renderer", "swiftshader");
	ASSERT_EQ(c.tags.size(), 2u);
	EXPECT_EQ(c.tags[0], std::make_pair(std::string("renderer"), std::string("swiftshader")));
	EXPECT_EQ(c.tags[1].first, "mode");
}

TEST(CrashContext, AnEmptyContextIsStillWholeJson)
{
	EXPECT_EQ(zx::web::ComputeCrashContextJson(CrashContext()),
		"{\"release\":\"\",\"dist\":\"\",\"dsn\":\"\",\"reporting\":false,\"map\":\"\",\"iwad\":\"\",\"files\":[],\"tags\":{}}");
}

TEST(CrashContext, TheJsonNamesTheGameAndEscapesWhatItQuotes)
{
	CrashContext c;
	c.release = "ForkUnderA@v0.3.2";
	c.dist = "abc123";
	c.dsn = "https://key@host/1";
	c.reporting = true;
	c.map = "MAP\"01";
	c.files = { "fua_core.pk3", "freedoom2.wad", "a\\b.pk3" };
	zx::web::ComputeSetCrashTag(c, "build", "web");
	zx::web::ComputeSetCrashTag(c, "line", "one\ntwo");
	EXPECT_EQ(zx::web::ComputeCrashContextJson(c),
		"{\"release\":\"ForkUnderA@v0.3.2\",\"dist\":\"abc123\",\"dsn\":\"https://key@host/1\",\"reporting\":true,"
		"\"map\":\"MAP\\\"01\",\"iwad\":\"freedoom2.wad\",\"files\":[\"fua_core.pk3\",\"freedoom2.wad\",\"a\\\\b.pk3\"],"
		"\"tags\":{\"build\":\"web\",\"line\":\"one\\ntwo\"}}");
}

TEST(CrashContext, OneFileHasNoIwad)
{
	CrashContext c;
	c.files = { "fua_core.pk3" };
	EXPECT_NE(zx::web::ComputeCrashContextJson(c).find("\"iwad\":\"\""), std::string::npos);
}

} // namespace
