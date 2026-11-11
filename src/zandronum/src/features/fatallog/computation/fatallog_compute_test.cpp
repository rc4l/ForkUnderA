#include "fatallog_compute.h"

#include <string>
#include <gtest/gtest.h>

using namespace zx::fatallog;

TEST(FatalLogText, OneLineMessageFollowsTheMarker)
{
	EXPECT_EQ(ComputeFatalLogText("Could not find map FEET01"), "fua: FATAL ERROR -- Could not find map FEET01\n");
}

TEST(FatalLogText, LaterLinesAreIndented)
{
	EXPECT_EQ(ComputeFatalLogText("Script error, \"a.pk3:decorate.txt\" line 2:\nExpected '{'\nGot ':'"),
		"fua: FATAL ERROR -- Script error, \"a.pk3:decorate.txt\" line 2:\n  Expected '{'\n  Got ':'\n");
}

TEST(FatalLogText, CarriageReturnsReadAsNewlines)
{
	EXPECT_EQ(ComputeFatalLogText("first\r\nsecond\rthird"), "fua: FATAL ERROR -- first\n  second\n  third\n");
}

TEST(FatalLogText, TrailingBlankLinesAndSpacesAreDropped)
{
	EXPECT_EQ(ComputeFatalLogText("broken\n\n \t\r\n"), "fua: FATAL ERROR -- broken\n");
}

TEST(FatalLogText, NullOrBlankMessageStillLogs)
{
	EXPECT_EQ(ComputeFatalLogText(nullptr), "fua: FATAL ERROR -- (no message)\n");
	EXPECT_EQ(ComputeFatalLogText(""), "fua: FATAL ERROR -- (no message)\n");
	EXPECT_EQ(ComputeFatalLogText("\n\n"), "fua: FATAL ERROR -- (no message)\n");
}

TEST(FatalLogText, StartsWithTheMarkerFuactlReads)
{
	EXPECT_EQ(ComputeFatalLogText("x").rfind(kFatalLogMarker, 0), 0u);
}

TEST(FatalLogClaim, OnlyTheFirstFatalIsLogged)
{
	bool logged = false;
	EXPECT_TRUE(ComputeFatalLogClaim(logged));
	EXPECT_TRUE(logged);
	EXPECT_FALSE(ComputeFatalLogClaim(logged));
	EXPECT_FALSE(ComputeFatalLogClaim(logged));
}
