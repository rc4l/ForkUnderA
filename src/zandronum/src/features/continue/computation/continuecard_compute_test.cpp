// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include <cstdlib>

#include "features/continue/computation/continuecard_compute.h"

using namespace zx;

namespace
{

ContinueCardMetrics Card( int rows, int vw = 640, int vh = 400 )
{
	ContinueCardMetrics m;
	m.rowCount = rows;
	m.virtualW = vw;
	m.virtualH = vh;
	return m;
}

} // namespace

TEST( ContinueCard, TheFlowingHalfNeverReachesTheRefusal )
{
	// THE BUG THIS UNIT EXISTS FOR. The refusal is anchored above the button while the rest of the
	// panel flows down from the top, and on a short card the two met -- the reason was drawn straight
	// across the date. Every row count, every reason length.
	for ( int rows = 1; rows <= 10; ++rows )
	{
		for ( int lines = 0; lines <= 3; ++lines )
		{
			const ContinueCardLayout l = ComputeContinueCard( Card( rows ), lines );

			// The floor must leave room for the reason and the button underneath it.
			EXPECT_LE( l.panelFlowLimit + ( lines * 10 ), l.buttonY - 3 )
				<< "rows " << rows << " lines " << lines;

			// ...and must still leave usable room above it, or the panel says nothing at all.
			EXPECT_GT( l.panelFlowLimit, l.panelY + 20 ) << "rows " << rows << " lines " << lines;
		}
	}
}

TEST( ContinueCard, TheButtonIsAlwaysInsideItsPanel )
{
	for ( int rows = 1; rows <= 10; ++rows )
	{
		const ContinueCardLayout l = ComputeContinueCard( Card( rows ), 3 );

		EXPECT_GE( l.buttonX, l.panelX );
		EXPECT_LE( l.buttonX + l.buttonW, l.panelX + l.panelW );
		EXPECT_GE( l.buttonY, l.panelY );
		EXPECT_LE( l.buttonY + l.buttonH, l.panelY + l.panelH );
	}
}

TEST( ContinueCard, TheTwoColumnsNeverOverlap )
{
	for ( int rows = 1; rows <= 10; ++rows )
	{
		const ContinueCardLayout l = ComputeContinueCard( Card( rows ), 0 );
		EXPECT_LE( l.listX + l.listW, l.panelX ) << "rows " << rows;
	}
}

TEST( ContinueCard, EverythingStaysInsideTheCard )
{
	for ( int rows = 1; rows <= 10; ++rows )
	{
		const ContinueCardLayout l = ComputeContinueCard( Card( rows ), 2 );

		EXPECT_GE( l.listX, l.cardX );
		EXPECT_LE( l.panelX + l.panelW, l.cardX + l.cardW );
		EXPECT_GE( l.listY, l.cardY );
		EXPECT_LE( l.panelY + l.panelH, l.cardY + l.cardH );
	}
}

TEST( ContinueCard, AShortListStillGetsAPanelTallEnoughToTalk )
{
	// The direct cause of the overlap: the panel was sized from the rows alone.
	const ContinueCardLayout one = ComputeContinueCard( Card( 1 ), 3 );
	const ContinueCardLayout many = ComputeContinueCard( Card( 7 ), 3 );

	EXPECT_GE( one.panelH, 112 );
	EXPECT_GE( one.panelFlowLimit - one.panelY, 40 ) << "no room to say anything";
	EXPECT_GE( many.panelH, one.panelH );
}

TEST( ContinueCard, TheListStopsGrowingAndStartsScrolling )
{
	const ContinueCardLayout seven = ComputeContinueCard( Card( 7 ), 0 );
	const ContinueCardLayout fifty = ComputeContinueCard( Card( 50 ), 0 );

	EXPECT_EQ( 7, seven.visibleRows );
	EXPECT_EQ( 7, fifty.visibleRows );
	EXPECT_EQ( seven.cardH, fifty.cardH ) << "a longer history must not grow the card";
}

TEST( ContinueCard, AnEmptyListStillHasOneRowOfCard )
{
	const ContinueCardLayout l = ComputeContinueCard( Card( 0 ), 0 );
	EXPECT_EQ( 1, l.visibleRows );
	EXPECT_GT( l.cardH, 0 );
}

TEST( ContinueCard, TheCardIsCentred )
{
	const ContinueCardLayout l = ComputeContinueCard( Card( 5 ), 0 );

	// Within a pixel: an odd card height cannot be centred exactly, and the remainder goes below.
	EXPECT_EQ( l.cardX, 640 - ( l.cardX + l.cardW ));
	EXPECT_LE( abs( l.cardY - ( 400 - ( l.cardY + l.cardH ))), 1 );
}

TEST( ContinueCard, ANarrowWindowGetsANarrowerCardRatherThanAClippedOne )
{
	const ContinueCardLayout l = ComputeContinueCard( Card( 5, 400, 300 ), 0 );

	EXPECT_LE( l.cardX + l.cardW, 400 );
	EXPECT_GE( l.cardX, 0 );
	EXPECT_LE( l.panelX + l.panelW, l.cardX + l.cardW );
}

TEST( ContinueCard, AbsurdMetricsDoNotDivideByZero )
{
	// A window mid-resize can report anything, and the layout is asked every frame.
	ContinueCardMetrics m = Card( 5 );
	m.rowHeight = 0;
	m.lineHeight = 0;
	m.maxVisibleRows = 0;

	const ContinueCardLayout l = ComputeContinueCard( m, 2 );
	EXPECT_GE( l.visibleRows, 1 );
	EXPECT_GT( l.cardH, 0 );
}

TEST( ContinueCard, AWindowTooNarrowForTheCardStillGetsACard )
{
	// Below about 200 units there is no room for the card's own margins, and shrinking it further
	// would leave a sliver. It stops shrinking and overhangs instead, which at least still reads.
	const ContinueCardLayout l = ComputeContinueCard( Card( 3, 120, 300 ), 0 );

	EXPECT_EQ( 160, l.cardW );
	EXPECT_GT( l.cardH, 0 );
	EXPECT_GE( l.visibleRows, 1 );
}
