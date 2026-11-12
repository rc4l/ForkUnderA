// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "computation/cmdlinearg_compute.h"

#include <gtest/gtest.h>

using zx::ComputeQuotedArgument;

TEST(QuotedArgument, APlainArgumentIsWrittenAsItIs)
{
	EXPECT_EQ("sv_banfile", ComputeQuotedArgument("sv_banfile"));
}

TEST(QuotedArgument, AnEmptyArgumentIsAnEmptyPairOfQuotes)
{
	EXPECT_EQ("\"\"", ComputeQuotedArgument(""));
}

TEST(QuotedArgument, ASpaceOrASemicolonIsQuoted)
{
	EXPECT_EQ("\"two words\"", ComputeQuotedArgument("two words"));
	// [rc4l] Unquoted, the console ran banlist2.txt as a command of its own.
	EXPECT_EQ("\"banlist1.txt;banlist2.txt\"", ComputeQuotedArgument("banlist1.txt;banlist2.txt"));
}

TEST(QuotedArgument, QuotesAreEscapedInsideQuotes)
{
	EXPECT_EQ("\"say \\\"hi\\\";\"", ComputeQuotedArgument("say \"hi\";"));
}
