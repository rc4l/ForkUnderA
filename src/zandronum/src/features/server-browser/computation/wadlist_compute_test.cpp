// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/wadlist_compute.h"

using zx::LayoutWadList;
using zx::WadListLayout;

namespace
{

std::vector<int> W(int a = -1, int b = -1, int c = -1, int d = -1, int e = -1, int f = -1)
{
	std::vector<int> v;
	const int in[6] = { a, b, c, d, e, f };
	for (int i = 0; i < 6; ++i) { if (in[i] >= 0) v.push_back(in[i]); }
	return v;
}

// Every file appears exactly once, in order, across the lines -- the invariant a wrapper must never
// break however the widths fall.
void ExpectContiguous(const WadListLayout &l)
{
	size_t expect = 0;
	for (size_t i = 0; i < l.lines.size(); ++i)
	{
		EXPECT_EQ(expect, l.lines[i].first) << "line " << i << " does not continue the previous";
		EXPECT_LT(l.lines[i].first, l.lines[i].end) << "line " << i << " is empty";
		expect = l.lines[i].end;
	}
	EXPECT_EQ(expect, l.shown);
}

} // namespace

// ------------------------------------------------------------ the empty and the trivial

TEST(WadList, NothingToLayOutIsNoLines)
{
	const WadListLayout l = LayoutWadList(std::vector<int>(), 10, 10, 100, 3);

	EXPECT_TRUE(l.lines.empty());
	EXPECT_EQ(0u, l.shown);
	EXPECT_FALSE(l.truncated);
}

TEST(WadList, OneFileThatFitsIsOneLine)
{
	const WadListLayout l = LayoutWadList(W(50), 10, 10, 100, 3);

	ASSERT_EQ(1u, l.lines.size());
	EXPECT_EQ(0u, l.lines[0].first);
	EXPECT_EQ(1u, l.lines[0].end);
	EXPECT_FALSE(l.truncated);
}

TEST(WadList, AFileWiderThanTheLineStillGetsDrawn)
{
	// [rc4l] The caller shortens the name. A layout that dropped it instead would hide a file the
	// player is about to download, which is the one thing this panel exists to tell them.
	const WadListLayout l = LayoutWadList(W(500), 10, 10, 100, 3);

	ASSERT_EQ(1u, l.lines.size());
	EXPECT_EQ(1u, l.shown);
	EXPECT_FALSE(l.truncated);
}

// ------------------------------------------------------------ wrapping

TEST(WadList, FilesPackOntoALineUntilTheyDoNotFit)
{
	// 30 + 10 + 30 = 70 fits in 100; adding a third would be 110.
	const WadListLayout l = LayoutWadList(W(30, 30, 30), 10, 10, 100, 0);

	ASSERT_EQ(2u, l.lines.size());
	EXPECT_EQ(2u, l.lines[0].end) << "two fit on the first line";
	EXPECT_EQ(3u, l.lines[1].end);
	ExpectContiguous(l);
}

TEST(WadList, TheSeparatorIsPaidForBetweenFilesAndNotBeforeTheFirst)
{
	// Three 30s with no separator cost would be 90 and fit. With two separators it is 110 and does
	// not, so charging the separator is the whole difference and it must not be charged at the edge.
	EXPECT_EQ(2u, LayoutWadList(W(30, 30, 30), 10, 10, 100, 0).lines.size());
	EXPECT_EQ(1u, LayoutWadList(W(30, 30, 30), 0, 10, 100, 0).lines.size());
}

TEST(WadList, AnExactFitDoesNotSpill)
{
	// 40 + 20 + 40 == 100 exactly. Off-by-one here would wrap a line that fits.
	const WadListLayout l = LayoutWadList(W(40, 40), 20, 10, 100, 0);

	ASSERT_EQ(1u, l.lines.size());
	EXPECT_EQ(2u, l.shown);
}

TEST(WadList, WithNoCapTheListRunsAsLongAsItLikes)
{
	// maxLines 0 is what an entry with nothing under the list gets.
	const WadListLayout l = LayoutWadList(W(90, 90, 90, 90, 90), 10, 10, 100, 0);

	EXPECT_EQ(5u, l.lines.size());
	EXPECT_EQ(5u, l.shown);
	EXPECT_FALSE(l.truncated) << "nothing was dropped, so nothing is truncated";
	ExpectContiguous(l);
}

// ------------------------------------------------------------ the cap

TEST(WadList, TheCapStopsTheListAndSaysSo)
{
	const WadListLayout l = LayoutWadList(W(90, 90, 90, 90, 90), 10, 10, 100, 3);

	EXPECT_EQ(3u, l.lines.size());
	EXPECT_TRUE(l.truncated);
	EXPECT_LT(l.shown, 5u);
}

TEST(WadList, ExactlyFillingTheCapIsNotTruncation)
{
	// Three lines, nothing left over. Marking this truncated would put an ellipsis on a complete
	// list and send the reader hunting for files that are all already on screen.
	const WadListLayout l = LayoutWadList(W(90, 90, 90), 10, 10, 100, 3);

	EXPECT_EQ(3u, l.lines.size());
	EXPECT_FALSE(l.truncated);
	EXPECT_EQ(3u, l.shown);
}

TEST(WadList, TruncatingTakesFilesOffUntilTheEllipsisFits)
{
	// Eight 40s at a separator of 10 pack two to a 100-wide line, so three lines cannot hold them and
	// the third is cut. That line would be 40 + 10 + 40 = 90, leaving 10 -- not enough for a 40-wide
	// marker, so one file comes off it.
	const std::vector<int> widths(8, 40);
	const WadListLayout l = LayoutWadList(widths, 10, 40, 100, 3);

	ASSERT_EQ(3u, l.lines.size());
	EXPECT_TRUE(l.truncated);
	EXPECT_EQ(1u, l.lines[2].end - l.lines[2].first) << "the last line gave up a file for the marker";
	ExpectContiguous(l);
}

TEST(WadList, ALineIsNeverEmptiedJustToFitTheMarker)
{
	// A huge marker against a full line. Showing one name and "..." beats showing "..." alone, and
	// the tooltip carries the rest either way.
	const WadListLayout l = LayoutWadList(W(90, 90, 90, 90), 10, 5000, 100, 3);

	ASSERT_EQ(3u, l.lines.size());
	EXPECT_EQ(1u, l.lines[2].end - l.lines[2].first);
	EXPECT_TRUE(l.truncated);
}

TEST(WadList, OneLineOfCapIsHonoured)
{
	const WadListLayout l = LayoutWadList(W(30, 30, 30, 30), 10, 10, 100, 1);

	EXPECT_EQ(1u, l.lines.size());
	EXPECT_TRUE(l.truncated);
}

// ------------------------------------------------------------ invariants

TEST(WadList, TheLinesAlwaysCoverAContiguousRunFromTheStart)
{
	// Swept, because every caller draws by walking the ranges: a gap would skip a file silently and
	// an overlap would draw one twice.
	const int widths[] = { 5, 25, 60, 95, 140 };

	for (int a = 0; a < 5; ++a)
	{
		for (int b = 0; b < 5; ++b)
		{
			for (int c = 0; c < 5; ++c)
			{
				for (int cap = 0; cap <= 3; ++cap)
				{
					const WadListLayout l =
						LayoutWadList(W(widths[a], widths[b], widths[c]), 10, 30, 100, cap);
					ExpectContiguous(l);

					EXPECT_EQ(l.truncated, l.shown < 3u)
						<< "truncated must mean exactly that something was left out";

					if (cap > 0)
						EXPECT_LE(l.lines.size(), static_cast<size_t>(cap));
				}
			}
		}
	}
}

TEST(WadList, ANarrowerColumnNeverShowsMoreFiles)
{
	// Monotonic in the width, which is the property that makes a resizing panel behave.
	size_t prev = 0;
	for (int width = 40; width <= 400; width += 20)
	{
		const WadListLayout l = LayoutWadList(W(30, 30, 30, 30, 30), 10, 20, width, 3);
		EXPECT_GE(l.shown, prev) << "shrank when the column grew to " << width;
		prev = l.shown;
	}
}

TEST(WadList, AFreshLineCoversNothing)
{
	// [rc4l] The default a caller gets before the layout fills it in: an empty half-open range, so
	// `first == end` and a loop over it does nothing rather than reading item zero.
	const zx::WadListLine line;

	EXPECT_EQ(0u, line.first);
	EXPECT_EQ(0u, line.end);
	EXPECT_EQ(line.first, line.end) << "an empty range, not a one-item one";
}
