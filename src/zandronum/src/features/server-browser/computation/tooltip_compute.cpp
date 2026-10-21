// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/server-browser/computation/tooltip_compute.h"

namespace zx
{

bool TooltipRectContains( int x, int y, int w, int h, int px, int py )
{
	if (( w <= 0 ) || ( h <= 0 ))
		return false;

	return ( px >= x ) && ( px < ( x + w )) && ( py >= y ) && ( py < ( y + h ));
}

std::vector<std::string> TooltipLines( const std::string &text )
{
	std::vector<std::string> out;

	// Nothing in, nothing out -- not one empty line, which would draw as an empty box.
	if ( text.empty( ))
		return out;

	std::string line;
	for ( size_t i = 0; i < text.size( ); ++i )
	{
		if ( text[i] == '\n' )
		{
			out.push_back( line );
			line.clear( );
			continue;
		}

		line.push_back( text[i] );
	}

	out.push_back( line );
	return out;
}

TooltipBox ComputeTooltipPlacement( int px, int py, int contentW, int contentH,
	int screenW, int screenH, int offset, int margin )
{
	TooltipBox out;
	out.w = contentW;
	out.h = contentH;

	// Preferred: down and to the right, so the pointer is not sitting on the text.
	out.x = px + offset;
	out.y = py + offset;

	// FLIP rather than merely slide. Near the right edge, sliding left would put the box under the
	// cursor; putting it on the other side of the pointer keeps the same clearance it had.
	if (( out.x + contentW ) > ( screenW - margin ))
		out.x = px - offset - contentW;
	if (( out.y + contentH ) > ( screenH - margin ))
		out.y = py - offset - contentH;

	// And clamp, for the case where neither side fits -- a box wider than the screen, or a pointer in
	// a corner. Left and top win over right and bottom: the start of the text is the part worth
	// keeping when something has to be lost.
	if (( out.x + contentW ) > ( screenW - margin ))
		out.x = screenW - margin - contentW;
	if (( out.y + contentH ) > ( screenH - margin ))
		out.y = screenH - margin - contentH;
	if ( out.x < margin )
		out.x = margin;
	if ( out.y < margin )
		out.y = margin;

	return out;
}

} // namespace zx
