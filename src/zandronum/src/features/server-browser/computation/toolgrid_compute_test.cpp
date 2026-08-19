// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/toolgrid_compute.h"

using zx::ComputeGridMove;
using zx::GridKey;
using zx::GridMove;

namespace
{

// The NEW tab's settings grid: FLAGS MAPS / GAMEPLAY SERVER.
const int kCount = 4;
const int kCols = 2;

GridMove Move(int sel, GridKey key) { return ComputeGridMove(sel, kCount, kCols, key); }

} // namespace

// ---------------------------------------------------------------- along a row

TEST(ToolGrid, LeftAndRightStayOnTheirOwnRow)
{
	EXPECT_EQ(1, Move(0, GridKey::Right).sel);
	EXPECT_EQ(0, Move(1, GridKey::Left).sel);
	EXPECT_EQ(3, Move(2, GridKey::Right).sel);
	EXPECT_EQ(2, Move(3, GridKey::Left).sel);
}

// Left off the start of a row has nowhere to go and must not wrap onto another row.
TEST(ToolGrid, TheStartOfARowIsAStop)
{
	EXPECT_EQ(0, Move(0, GridKey::Left).sel);
	EXPECT_EQ(2, Move(2, GridKey::Left).sel);
	EXPECT_FALSE(Move(0, GridKey::Left).leaves);
}

// Right off the right-hand end leaves the grid -- the foot's buttons are over there.
TEST(ToolGrid, TheRightHandEndOfEitherRowLeavesTheGrid)
{
	EXPECT_TRUE(Move(1, GridKey::Right).leaves);
	EXPECT_TRUE(Move(3, GridKey::Right).leaves);

	// Leaving does not also move the cursor, so coming back lands where it was.
	EXPECT_EQ(1, Move(1, GridKey::Right).sel);
	EXPECT_EQ(3, Move(3, GridKey::Right).sel);

	// And a cell with a neighbour still just moves.
	EXPECT_FALSE(Move(0, GridKey::Right).leaves);
	EXPECT_FALSE(Move(2, GridKey::Right).leaves);
}

// ---------------------------------------------------------------- between rows

TEST(ToolGrid, DownCrossesToTheRowBelowAndStopsAtTheBottom)
{
	EXPECT_EQ(2, Move(0, GridKey::Down).sel);
	EXPECT_EQ(3, Move(1, GridKey::Down).sel);
	EXPECT_EQ(2, Move(2, GridKey::Down).sel);
	EXPECT_EQ(3, Move(3, GridKey::Down).sel);

	EXPECT_FALSE(Move(2, GridKey::Down).leaves);
}

TEST(ToolGrid, UpCrossesUpAndLeavesOffTheTop)
{
	EXPECT_EQ(0, Move(2, GridKey::Up).sel);
	EXPECT_FALSE(Move(2, GridKey::Up).leaves);

	EXPECT_TRUE(Move(0, GridKey::Up).leaves);
	EXPECT_TRUE(Move(1, GridKey::Up).leaves);
	EXPECT_EQ(1, Move(1, GridKey::Up).sel);		// leaving does not also move the cursor
}

// ---------------------------------------------------------------- a short last row

// Three in a 2-wide grid leaves one alone on the bottom; RIGHT from it has no cell to select.
TEST(ToolGrid, AShortLastRowHasNoCellToTheRight)
{
	EXPECT_EQ(2, ComputeGridMove(2, 3, 2, GridKey::Right).sel);
	EXPECT_TRUE(ComputeGridMove(2, 3, 2, GridKey::Right).leaves);
	EXPECT_EQ(2, ComputeGridMove(2, 3, 2, GridKey::Down).sel);
	EXPECT_EQ(0, ComputeGridMove(2, 3, 2, GridKey::Up).sel);
}

// And DOWN from a top cell with nothing under it stays put rather than selecting past the end.
TEST(ToolGrid, DownWithNothingUnderItStaysPut)
{
	EXPECT_EQ(1, ComputeGridMove(1, 3, 2, GridKey::Down).sel);
}

// ---------------------------------------------------------------- degenerate input

TEST(ToolGrid, AnEmptyOrWidthlessGridAnswersWithoutDividing)
{
	EXPECT_EQ(0, ComputeGridMove(0, 0, 2, GridKey::Down).sel);
	EXPECT_FALSE(ComputeGridMove(0, 0, 2, GridKey::Up).leaves);
	EXPECT_EQ(3, ComputeGridMove(3, 4, 0, GridKey::Left).sel);
}

TEST(ToolGrid, ASelectionOutsideTheGridIsCorrectedFirst)
{
	EXPECT_EQ(2, ComputeGridMove(-5, kCount, kCols, GridKey::Down).sel);
	EXPECT_EQ(3, ComputeGridMove(99, kCount, kCols, GridKey::Right).sel);
	EXPECT_EQ(1, ComputeGridMove(99, kCount, kCols, GridKey::Up).sel);
}

// ---------------------------------------------------------------- leaving the wad list rightwards

using zx::ComputeRightExitFromList;
using zx::RightExit;

TEST(ToolGrid, RightOutOfTheListLandsOnTheLoadOrderWhenThereIsOne)
{
	EXPECT_EQ(RightExit::LoadOrder, ComputeRightExitFromList(true));
}

// An empty order has no row to focus, so the key carries on to the foot rather than landing on
// something that is not drawn.
TEST(ToolGrid, RightOutOfTheListSkipsAnEmptyLoadOrder)
{
	EXPECT_EQ(RightExit::Foot, ComputeRightExitFromList(false));
}

// ---------------------------------------------------------------- walking a list with exits

using zx::ComputeListStep;
using zx::ListStep;

TEST(ToolGrid, AListWalksItsOwnRowsFirst)
{
	EXPECT_EQ(ListStep::Move, ComputeListStep(0, 3, 1));
	EXPECT_EQ(ListStep::Move, ComputeListStep(1, 3, 1));
	EXPECT_EQ(ListStep::Move, ComputeListStep(2, 3, -1));
}

// The bug this exists for: clamping at both ends is a region the keyboard cannot leave.
TEST(ToolGrid, EitherEndOfAListLeavesItRatherThanClamping)
{
	EXPECT_EQ(ListStep::LeaveUp, ComputeListStep(0, 3, -1));
	EXPECT_EQ(ListStep::LeaveDown, ComputeListStep(2, 3, 1));
}

// An empty list is not somewhere focus may sit, so it never swallows the key.
TEST(ToolGrid, AnEmptyListIsPassedStraightThrough)
{
	EXPECT_EQ(ListStep::LeaveUp, ComputeListStep(0, 0, -1));
	EXPECT_EQ(ListStep::LeaveDown, ComputeListStep(0, 0, 1));
}

// A single row has no neighbours, so both directions leave.
TEST(ToolGrid, ASingleRowLeavesInBothDirections)
{
	EXPECT_EQ(ListStep::LeaveUp, ComputeListStep(0, 1, -1));
	EXPECT_EQ(ListStep::LeaveDown, ComputeListStep(0, 1, 1));
}

// ---------------------------------------------------------------- leaving the foot

using zx::ComputeFootExit;
using zx::FootExit;

TEST(ToolGrid, LeftOffTheLeftmostFootButtonReachesTheSettingsGrid)
{
	EXPECT_EQ(FootExit::SettingsGrid, ComputeFootExit(GridKey::Left, 0, true));
	EXPECT_EQ(FootExit::SettingsGrid, ComputeFootExit(GridKey::Left, 0, false));
}

TEST(ToolGrid, LeftElsewhereOnTheFootJustWalksTheRow)
{
	EXPECT_EQ(FootExit::StayOnRow, ComputeFootExit(GridKey::Left, 1, true));
}

// Up must not walk into a load order with nothing in it -- that is where focus was being lost.
TEST(ToolGrid, UpFromTheFootReachesTheLoadOrderWhenThereIsOne)
{
	EXPECT_EQ(FootExit::LoadOrder, ComputeFootExit(GridKey::Up, 0, true));
	EXPECT_EQ(FootExit::LoadOrder, ComputeFootExit(GridKey::Up, 1, true));
}

// With nothing loaded there is nothing to play, so the way out of the foot is the top of the screen
// rather than the settings for a server that cannot be started yet.
TEST(ToolGrid, UpFromTheFootWithAnEmptyOrderGoesToTheIwadRow)
{
	EXPECT_EQ(FootExit::IwadRow, ComputeFootExit(GridKey::Up, 0, false));
	EXPECT_EQ(FootExit::IwadRow, ComputeFootExit(GridKey::Up, 1, false));
}

// LEFT is unchanged by any of that: the grid is beside the foot whatever the order holds.
TEST(ToolGrid, LeftOffTheFootIsTheGridWhateverTheOrderHolds)
{
	EXPECT_EQ(FootExit::SettingsGrid, ComputeFootExit(GridKey::Left, 0, true));
	EXPECT_EQ(FootExit::SettingsGrid, ComputeFootExit(GridKey::Left, 0, false));
}

TEST(ToolGrid, TheFootKeepsRightAndDownToItself)
{
	EXPECT_EQ(FootExit::StayOnRow, ComputeFootExit(GridKey::Right, 0, true));
	EXPECT_EQ(FootExit::StayOnRow, ComputeFootExit(GridKey::Down, 0, false));
}

// ---------------------------------------------------------------- a modal is a loop

using zx::ComputeModalStep;
using zx::ModalPos;
using zx::ModalRegion;

namespace
{

ModalPos Body(int i) { return ModalPos(ModalRegion::Body, i); }
ModalPos Foot(int i) { return ModalPos(ModalRegion::Footer, i); }

ModalPos Step(ModalPos p, int step) { return ComputeModalStep(p, 5, 4, step); }

} // namespace

TEST(ModalStep, TheBodyWalksItsOwnRowsFirst)
{
	EXPECT_EQ(ModalRegion::Body, Step(Body(0), 1).region);
	EXPECT_EQ(1, Step(Body(0), 1).index);
	EXPECT_EQ(2, Step(Body(3), -1).index);
}

// The bug this exists for: the top of a list used to be a dead end.
TEST(ModalStep, OffTheTopOfTheBodyIsTheFooter)
{
	EXPECT_EQ(ModalRegion::Footer, Step(Body(0), -1).region);
	EXPECT_EQ(0, Step(Body(0), -1).index);
}

TEST(ModalStep, OffTheBottomOfTheBodyIsTheFooterToo)
{
	EXPECT_EQ(ModalRegion::Footer, Step(Body(4), 1).region);
	EXPECT_EQ(0, Step(Body(4), 1).index);
}

// Coming back lands at the end nearest the key, so the loop reads the same in both directions.
TEST(ModalStep, OffTheFooterIsTheEndOfTheBodyNearestTheKey)
{
	EXPECT_EQ(ModalRegion::Body, Step(Foot(0), -1).region);
	EXPECT_EQ(4, Step(Foot(0), -1).index);

	EXPECT_EQ(ModalRegion::Body, Step(Foot(0), 1).region);
	EXPECT_EQ(0, Step(Foot(0), 1).index);
}

// One press each way returns to where it started, which is what makes it a loop rather than a walk.
TEST(ModalStep, TheLoopClosesInBothDirections)
{
	EXPECT_EQ(0, Step(Step(Body(0), -1), 1).index);
	EXPECT_EQ(ModalRegion::Body, Step(Step(Body(0), -1), 1).region);

	EXPECT_EQ(ModalRegion::Body, Step(Step(Body(4), 1), -1).region);
	EXPECT_EQ(4, Step(Step(Body(4), 1), -1).index);
}

// An empty body is not somewhere focus may sit, so the footer keeps the key either way.
TEST(ModalStep, AnEmptyBodyLeavesTheKeyOnTheFooter)
{
	EXPECT_EQ(ModalRegion::Footer, ComputeModalStep(Foot(0), 0, 2, -1).region);
	EXPECT_EQ(ModalRegion::Footer, ComputeModalStep(Foot(0), 0, 2, 1).region);
	EXPECT_EQ(ModalRegion::Footer, ComputeModalStep(Body(0), 0, 2, -1).region);
}

// A box with only a DONE is still a loop; footCount is corrected rather than trusted.
TEST(ModalStep, ABoxWithOneButtonStillLoops)
{
	EXPECT_EQ(ModalRegion::Footer, ComputeModalStep(Body(0), 3, 1, -1).region);
	EXPECT_EQ(ModalRegion::Body, ComputeModalStep(Foot(0), 3, 0, 1).region);
}
