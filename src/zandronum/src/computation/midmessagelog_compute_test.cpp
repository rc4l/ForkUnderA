// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 MGOOOOOO

#include <gtest/gtest.h>

#include "computation/midmessagelog_compute.h"

using zx::ComputeMidMessageLogText;

TEST(MidMessageLog, TheMessageSitsBetweenZandronumsLogBars)
{
	// [MGOOOOOO] Byte for byte what Zandronum 3.3 writes, which log readers such as MM8BDM's TESTING room parse.
	EXPECT_EQ(ComputeMidMessageLogText("TST AUTODONE"),
		"\n<------------------------------->\nTST AUTODONE\n<------------------------------->\n");
}

TEST(MidMessageLog, ColourCodesAndLineBreaksPassThroughUntouched)
{
	EXPECT_EQ(ComputeMidMessageLogText("\x1c" "Dline one\nline two"),
		"\n<------------------------------->\n\x1c" "Dline one\nline two\n<------------------------------->\n");
}

TEST(MidMessageLog, AnEmptyMessageStillWritesBothBars)
{
	EXPECT_EQ(ComputeMidMessageLogText(""), "\n<------------------------------->\n\n<------------------------------->\n");
}
