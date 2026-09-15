// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include <gtest/gtest.h>

#include "computation/virtualspace_compute.h"

using namespace zx;

TEST( VirtualSpace, ASquareOnFourByThreeGrowsTheShortAxis )
{
	// 640x400 layout on a 4:3 screen: width runs out first, so the space is taller than 400.
	const VirtualSpace s = ComputeVirtualSpace( 800, 600, 640, 400 );

	EXPECT_EQ( 640, s.width );
	EXPECT_EQ( 480, s.height );
}

TEST( VirtualSpace, AWideScreenGrowsTheWidth )
{
	const VirtualSpace s = ComputeVirtualSpace( 1920, 800, 640, 400 );

	EXPECT_EQ( 400, s.height );
	EXPECT_EQ( 960, s.width );
}

TEST( VirtualSpace, AScreenOfTheLayoutsOwnShapeChangesNothing )
{
	const VirtualSpace s = ComputeVirtualSpace( 1280, 800, 640, 400 );

	EXPECT_EQ( 640, s.width );
	EXPECT_EQ( 400, s.height );
}

TEST( VirtualSpace, TheSpaceAlwaysHasTheScreensAspect )
{
	// The property the whole approach rests on: with the aspects equal, the scale is one number and
	// two rectangles sharing an edge cannot land a pixel apart.
	const int screens[][2] = { {640,400}, {800,600}, {1920,1080}, {1366,768}, {1024,768}, {2560,1080} };

	for ( int i = 0; i < 6; ++i )
	{
		const VirtualSpace s = ComputeVirtualSpace( screens[i][0], screens[i][1], 640, 400 );

		// width/height == screenW/screenH, cross-multiplied and allowing integer truncation.
		const int lhs = s.width * screens[i][1];
		const int rhs = s.height * screens[i][0];
		EXPECT_LE( abs( lhs - rhs ), screens[i][1] + screens[i][0] )
			<< "screen " << screens[i][0] << "x" << screens[i][1];
	}
}

TEST( VirtualSpace, ANonsenseScreenAnswersTheNominalSize )
{
	// Asked every frame, including mid-resize when the window can report anything.
	const VirtualSpace zero = ComputeVirtualSpace( 0, 0, 640, 400 );
	EXPECT_EQ( 640, zero.width );
	EXPECT_EQ( 400, zero.height );

	const VirtualSpace neg = ComputeVirtualSpace( -5, -5, 640, 400 );
	EXPECT_EQ( 640, neg.width );
	EXPECT_EQ( 400, neg.height );

	const VirtualSpace noLayout = ComputeVirtualSpace( 800, 600, 0, 0 );
	EXPECT_EQ( 0, noLayout.width );
}

TEST( VirtualSpace, TheRoundTripComesBackWhereItStarted )
{
	// What the shared inverse is FOR: the pointer must land where the drawing put it.
	const int screenW = 1920, virtualW = 960;

	for ( int v = 0; v <= 960; v += 37 )
	{
		const int px = VirtualToScreen( v, screenW, virtualW );

		const int atZero = VirtualToScreen( 0, screenW, virtualW );
		const int atSpan = VirtualToScreen( 100, screenW, virtualW );

		EXPECT_LE( abs( ScreenToVirtual( px, atZero, atSpan, 100 ) - v ), 1 ) << "at " << v;
	}
}

TEST( VirtualSpace, AZeroSpanDoesNotDivideByZero )
{
	// A window one pixel wide maps every layout unit to the same pixel, and the inverse has nothing
	// to work from.
	EXPECT_EQ( 0, ScreenToVirtual( 50, 10, 10, 100 ));
}

TEST( VirtualSpace, AZeroVirtualSizeDoesNotDivideByZero )
{
	EXPECT_EQ( 0, VirtualToScreen( 50, 640, 0 ));
}
