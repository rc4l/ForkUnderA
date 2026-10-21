// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "zx_focusglow.h"

#include "doomtype.h"
#include "v_video.h"
#include "v_palette.h"
#include "menu/menu.h"

#include <math.h>

namespace zx
{

void DrawFocusGlow( int screenX, int screenY, int scaleSpan )
{
	// Slow breath, never all the way out: a marker that vanishes is a marker you have to hunt for on
	// the frame it happens to be invisible.
	const double phase = ( DMenu::MenuTime % 70 ) / 70.0;
	const float breath = 0.72f + 0.28f * static_cast<float>( fabs( 1.0 - 2.0 * phase ));

	// Plain comparisons rather than MAX: this file sees the strong Fixed type, and MAX picks its
	// overload, which does not convert back to int.
	const int span = ( scaleSpan > 1 ) ? scaleSpan : 1;

	// Four shells, widest and faintest first. Concentric rather than a true radial ramp because Dim
	// takes rectangles, and at this size the difference is not visible; what IS visible is the
	// absence of a hard edge.
	const int kShells = 4;
	for ( int shell = kShells - 1; shell >= 0; --shell )
	{
		// Virtual radii of 3, 6, 9, 12, fixed in the caller's coordinate space, so the orb is the
		// same size beside a tab as beside a row.
		const int scaled = ( span * ( shell + 1 ) * 3 ) / 100;
		const int radius = ( scaled > 1 ) ? scaled : 1;
		const float alpha = breath * ( 0.10f + 0.16f * ( kShells - 1 - shell ));

		for ( int dy = -radius; dy <= radius; ++dy )
		{
			// A circle, row by row: half-width falls off as the square root, which is what stops the
			// shells reading as stacked squares.
			const int half = static_cast<int>( sqrt( double( radius * radius - dy * dy )));
			if ( half <= 0 )
				continue;

			screen->Dim( PalEntry( 190, 225, 255 ), alpha, screenX - half, screenY + dy, half * 2, 1 );
		}
	}
}

} // namespace zx
