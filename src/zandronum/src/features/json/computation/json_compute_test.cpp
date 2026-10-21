// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/json/computation/json_compute.h"

#include <gtest/gtest.h>

using zx::json::ComputeJsonEscape;
using zx::json::ComputeJsonQuoted;
using zx::json::JsonControls;

TEST(Json, QuotesBackslashesAndWhitespaceAreEscapedByName)
{
	EXPECT_EQ("he said \\\"hi\\\"\\r\\n\\tpath\\\\x", ComputeJsonEscape("he said \"hi\"\r\n\tpath\\x"));
}

// [rc4l] The bridge escaped a control character while the sampler and the preset writer dropped it, so a symbol with one read differently by writer.
TEST(Json, OtherControlCharactersBecomeUnicodeEscapes)
{
	EXPECT_EQ("\\u0001a\\u001f", ComputeJsonEscape(std::string("\x01") + "a" + "\x1f"));
}

TEST(Json, ADroppingWriterLeavesThemOutButKeepsTheNamedOnes)
{
	EXPECT_EQ("bellend\\n", ComputeJsonEscape(std::string("bell") + '\a' + "end\n", JsonControls::Drop));
}

TEST(Json, PlainTextAndHighBytesPassThrough)
{
	EXPECT_EQ("ok \xc3\xa9", ComputeJsonEscape("ok \xc3\xa9"));
	EXPECT_EQ("", ComputeJsonEscape(""));
}

TEST(Json, AQuotedStringIsEscapedInQuotes)
{
	EXPECT_EQ("\"a\\\"b\"", ComputeJsonQuoted("a\"b"));
	EXPECT_EQ("\"bell\"", ComputeJsonQuoted(std::string("bell") + '\a', JsonControls::Drop));
}
