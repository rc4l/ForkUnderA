// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "scrollview_compute.h"

using namespace zx;

namespace
{

// A viewport 100 tall, and a row height that divides into it awkwardly on purpose.
const int kTop = 200;
const int kBottom = 300;
const int kRowH = 18;

} // namespace

// ---------------------------------------------------------------- worth drawing at all

TEST( ScrollView, ARowInTheMiddleIsVisible )
{
	EXPECT_TRUE( RowIntersectsView( 240, kRowH, kTop, kBottom ));
	EXPECT_TRUE( RowFullyInView( 240, kRowH, kTop, kBottom ));
}

TEST( ScrollView, ARowEntirelyOutsideIsNotDrawn )
{
	// Well clear on each side. These are the rows the panel skips outright.
	EXPECT_FALSE( RowIntersectsView( 100, kRowH, kTop, kBottom ));
	EXPECT_FALSE( RowIntersectsView( 400, kRowH, kTop, kBottom ));
}

TEST( ScrollView, TouchingAnEdgeIsNotOverlappingIt )
{
	// [rc4l] Half-open, and it has to be: a row whose bottom edge IS the viewport top covers no
	// pixels inside it. Counting that as visible would have the panel draw a row nobody can see and,
	// worse, would put it one row out of step with the scrollbar, which measures actual content.
	EXPECT_FALSE( RowIntersectsView( kTop - kRowH, kRowH, kTop, kBottom ));
	EXPECT_FALSE( RowIntersectsView( kBottom, kRowH, kTop, kBottom ));

	// One pixel further in each direction and there is something to see.
	EXPECT_TRUE( RowIntersectsView( kTop - kRowH + 1, kRowH, kTop, kBottom ));
	EXPECT_TRUE( RowIntersectsView( kBottom - 1, kRowH, kTop, kBottom ));
}

// ---------------------------------------------------------------- worth lettering

TEST( ScrollView, ARowHangingOverAnEdgeIsVisibleButNotWhole )
{
	// THE DISTINCTION THE WHOLE UNIT EXISTS FOR. These rows draw -- their backgrounds clip to a
	// sliver, which is the visual cue that there is more -- and their text does not, because text
	// does not clip on the path the browser draws through and would carry on past a background that
	// has already stopped.
	EXPECT_TRUE( RowIntersectsView( kTop - 4, kRowH, kTop, kBottom ));
	EXPECT_FALSE( RowFullyInView( kTop - 4, kRowH, kTop, kBottom ));

	EXPECT_TRUE( RowIntersectsView( kBottom - 4, kRowH, kTop, kBottom ));
	EXPECT_FALSE( RowFullyInView( kBottom - 4, kRowH, kTop, kBottom ));
}

TEST( ScrollView, ARowFlushAgainstAnEdgeIsWhole )
{
	// Closed at both ends, where the intersection test is open. A row sitting exactly on an edge is
	// entirely on screen and gets its lettering -- refusing it would blank the first and last rows
	// of a form that fits perfectly, which is the common case.
	EXPECT_TRUE( RowFullyInView( kTop, kRowH, kTop, kBottom ));
	EXPECT_TRUE( RowFullyInView( kBottom - kRowH, kRowH, kTop, kBottom ));
}

TEST( ScrollView, ARowTallerThanTheViewportIsNeverWhole )
{
	EXPECT_TRUE( RowIntersectsView( kTop, 500, kTop, kBottom ));
	EXPECT_FALSE( RowFullyInView( kTop, 500, kTop, kBottom ));
}

// ---------------------------------------------------------------- staying inside the scroll

TEST( ScrollView, ClampKeepsAScrollBetweenItsEnds )
{
	EXPECT_EQ( 0, ClampScroll( -30, 80 ));
	EXPECT_EQ( 40, ClampScroll( 40, 80 ));
	EXPECT_EQ( 80, ClampScroll( 200, 80 ));
}

TEST( ScrollView, ContentShorterThanTheViewportSettlesAtZero )
{
	// [rc4l] maxScroll goes NEGATIVE when everything fits, and the host form does exactly that: it is
	// one row shorter while a server is running. Clamping high-then-low lands on 0; the other order
	// would land on the negative ceiling and scroll a panel with nothing to scroll.
	EXPECT_EQ( 0, ClampScroll( 25, -12 ));
	EXPECT_EQ( 0, ClampScroll( 0, -12 ));
}

// ---------------------------------------------------------------- following the focus

TEST( ScrollView, AVisibleRowDoesNotMoveTheView )
{
	EXPECT_EQ( 30, ScrollToReveal( 30, 240, kRowH, kTop, kBottom, 200 ));
}

TEST( ScrollView, ARowAboveTheViewComesDownByExactlyItsOverhang )
{
	// Six pixels above the top, so the view moves six -- not a page, not to the top. Arrowing up a
	// form should nudge.
	EXPECT_EQ( 24, ScrollToReveal( 30, kTop - 6, kRowH, kTop, kBottom, 200 ));
}

TEST( ScrollView, ARowBelowTheViewComesUpByExactlyItsOverhang )
{
	// Its bottom sits 8 past the boundary.
	EXPECT_EQ( 38, ScrollToReveal( 30, kBottom - kRowH + 8, kRowH, kTop, kBottom, 200 ));
}

TEST( ScrollView, RevealingNeverLeavesTheScrollableRange )
{
	// Revealing is still a scroll and obeys the same ends. Without this a row far off one side would
	// be chased past the end of the content and the panel would show blank.
	EXPECT_EQ( 0, ScrollToReveal( 5, kTop - 400, kRowH, kTop, kBottom, 200 ));
	EXPECT_EQ( 200, ScrollToReveal( 190, kBottom + 400, kRowH, kTop, kBottom, 200 ));
}

TEST( ScrollView, ARowTallerThanTheViewportSettlesOnItsTop )
{
	// Both tests would fire on a row this size. Only the first does, so it lands showing the top of
	// the row -- which is where its label is -- instead of flipping between the two corrections.
	EXPECT_EQ( 20, ScrollToReveal( 30, kTop - 10, 400, kTop, kBottom, 200 ));
}
