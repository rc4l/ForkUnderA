// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "features/web/computation/webargs_compute.h"

using namespace zx::web;

typedef std::vector<std::string> Args;

TEST(WebArgs, TheNamedKeysBecomeTheirFlags)
{
	EXPECT_EQ((Args{ "-iwad", "freedoom2.wad", "-file", "a.pk3", "b.pk3", "+map", "MAP07", "-skill", "4" }),
		ComputeArgsFromQuery("?iwad=freedoom2.wad&file=a.pk3,b.pk3&map=MAP07&skill=4"));
	EXPECT_EQ((Args{ "-warp", "1", "3" }), ComputeArgsFromQuery("warp=1+3"));
}

// [rc4l] args is the command line as typed, split on spaces, after decoding.
TEST(WebArgs, ArgsIsTheCommandLineAsTyped)
{
	EXPECT_EQ((Args{ "-nomonsters", "+set", "fua_dg_verbose", "1" }),
		ComputeArgsFromQuery("?args=-nomonsters%20%2Bset+fua_dg_verbose%201"));
}

// [rc4l] Keys the page doesn't know, keys with nothing after them and empty pairs are dropped, and empty list entries vanish.
TEST(WebArgs, WhatIsNotUnderstoodIsDropped)
{
	EXPECT_EQ((Args{ "-file", "a.pk3" }), ComputeArgsFromQuery("?&utm_source=x&map=&iwad&file=,a.pk3,"));
	EXPECT_TRUE(ComputeArgsFromQuery("").empty());
	EXPECT_TRUE(ComputeArgsFromQuery("?").empty());
}

TEST(WebArgs, ValuesDecodeAsAFormEncodesThem)
{
	EXPECT_EQ("my wad.pk3", ComputeDecodeQueryValue("my%20wad.pk3"));
	EXPECT_EQ("a b", ComputeDecodeQueryValue("a+b"));
	EXPECT_EQ("+map", ComputeDecodeQueryValue("%2Bmap"));
	EXPECT_EQ("%2", ComputeDecodeQueryValue("%2"));
	EXPECT_EQ("%zz", ComputeDecodeQueryValue("%zz"));
	EXPECT_EQ("%4g", ComputeDecodeQueryValue("%4g"));
	EXPECT_EQ("\xff", ComputeDecodeQueryValue("%fF"));
}
