// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gtest/gtest.h"
#include "features/server-browser/computation/liverow_compute.h"

using zx::PaintListRow;
using zx::RowBand;
using zx::RowLabel;
using zx::RowPaint;

// ------------------------------------------------------------------ nothing

TEST( LiveRow, PlainRowIsUnmarked )
{
	const RowPaint paint = PaintListRow( false, false, false );

	EXPECT_EQ( RowBand::None, paint.band );
	EXPECT_EQ( RowLabel::Plain, paint.label );
}

TEST( LiveRow, DefaultIsThePlainRow )
{
	// The default constructor has to agree with what PaintListRow says about a row that is nothing
	// in particular, or a RowPaint that was declared and not assigned would draw a band.
	const RowPaint fresh;

	EXPECT_EQ( RowBand::None, fresh.band );
	EXPECT_EQ( RowLabel::Plain, fresh.label );
}

// -------------------------------------------------------------------- hover

TEST( LiveRow, HoverGetsABandAndNothingElse )
{
	// [rc4l] Hover is a hint about a click that has not happened. It may say where the pointer is;
	// it may not recolour the label, because the label is where the row says what it IS.
	const RowPaint paint = PaintListRow( false, false, true );

	EXPECT_EQ( RowBand::Hover, paint.band );
	EXPECT_EQ( RowLabel::Plain, paint.label );
}

TEST( LiveRow, SelectionBeatsHover )
{
	const RowPaint paint = PaintListRow( true, false, true );

	EXPECT_EQ( RowBand::Selection, paint.band );
	EXPECT_EQ( RowLabel::Selected, paint.label );
}

TEST( LiveRow, LiveBeatsHover )
{
	// The bug this pins: the hosting catalogue used to let a hovered running row take the hover's
	// colour, so the one state worth marking vanished exactly when you pointed at it.
	const RowPaint paint = PaintListRow( false, true, true );

	EXPECT_EQ( RowBand::Live, paint.band );
	EXPECT_EQ( RowLabel::Live, paint.label );
}

// --------------------------------------------------------------- selection

TEST( LiveRow, SelectedRowIsBandedAndLabelled )
{
	const RowPaint paint = PaintListRow( true, false, false );

	EXPECT_EQ( RowBand::Selection, paint.band );
	EXPECT_EQ( RowLabel::Selected, paint.label );
}

// -------------------------------------------------------------------- live

TEST( LiveRow, LiveRowIsGreenWhileTheSelectionIsElsewhere )
{
	// The whole reason the band carries live: the selection moves every time an arrow key is
	// pressed, and being connected does not, so the mark has to survive the selection walking away.
	const RowPaint paint = PaintListRow( false, true, false );

	EXPECT_EQ( RowBand::Live, paint.band );
	EXPECT_EQ( RowLabel::Live, paint.label );
}

TEST( LiveRow, LiveAndSelectedSplitsTheTwoJobs )
{
	// Both facts are true and both get said, because they are said in different places. The band
	// stays live, the label reports the selection. Nothing here is a tie to be broken.
	const RowPaint paint = PaintListRow( true, true, false );

	EXPECT_EQ( RowBand::Live, paint.band );
	EXPECT_EQ( RowLabel::Selected, paint.label );
}

TEST( LiveRow, LiveIsNeverLostToAnythingElse )
{
	// All eight combinations, one property: if the row is live, the band says so. This is the
	// promise the two lists depend on, and enumerating it is cheaper than trusting the reader to
	// notice a later branch that returns early.
	for ( int bits = 0; bits < 8; ++bits )
	{
		const bool selected = (( bits & 1 ) != 0 );
		const bool live = (( bits & 2 ) != 0 );
		const bool hovered = (( bits & 4 ) != 0 );

		const RowPaint paint = PaintListRow( selected, live, hovered );

		if ( live )
		{
			EXPECT_EQ( RowBand::Live, paint.band ) << "bits " << bits;
		}
		else
		{
			EXPECT_NE( RowBand::Live, paint.band ) << "bits " << bits;
			EXPECT_NE( RowLabel::Live, paint.label ) << "bits " << bits;
		}
	}
}

TEST( LiveRow, ConstructorKeepsWhatItIsGiven )
{
	const RowPaint made( RowBand::Selection, RowLabel::Live );

	EXPECT_EQ( RowBand::Selection, made.band );
	EXPECT_EQ( RowLabel::Live, made.label );
}
