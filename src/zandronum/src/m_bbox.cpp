//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Marisa Heit
// Copyright 2002-2016 Christoph Oelckers
// Copyright 2012-2024 Zandronum Development Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
// DESCRIPTION:
//		Main loop menu stuff.
//		Random number LUT.
//		Default Config File.
//		PCX Screenshots.
//
//-----------------------------------------------------------------------------

#include "m_bbox.h"
#include "p_local.h"

void FBoundingBox::AddToBox (fixed_t x, fixed_t y)
{
	if (x < m_Box[BOXLEFT])
		m_Box[BOXLEFT] = x;
	if (x > m_Box[BOXRIGHT])
		m_Box[BOXRIGHT] = x;

	if (y < m_Box[BOXBOTTOM])
		m_Box[BOXBOTTOM] = y;
	if (y > m_Box[BOXTOP])
		m_Box[BOXTOP] = y;
}

//==========================================================================
//
// FBoundingBox :: BoxOnLineSide
//
// Considers the line to be infinite
// Returns side 0 or 1, -1 if box crosses the line.
//
//==========================================================================

int FBoundingBox::BoxOnLineSide (const line_t *ld) const
{
	int p1;
	int p2;
		
	switch (ld->slopetype)
	{
	case ST_HORIZONTAL:
		p1 = m_Box[BOXTOP] > ld->v1->y;
		p2 = m_Box[BOXBOTTOM] > ld->v1->y;
		if (ld->dx < 0)
		{
			p1 ^= 1;
			p2 ^= 1;
		}
		break;
		
	case ST_VERTICAL:
		p1 = m_Box[BOXRIGHT] < ld->v1->x;
		p2 = m_Box[BOXLEFT] < ld->v1->x;
		if (ld->dy < 0)
		{
			p1 ^= 1;
			p2 ^= 1;
		}
		break;
		
	case ST_POSITIVE:
		p1 = P_PointOnLineSide (m_Box[BOXLEFT], m_Box[BOXTOP], ld);
		p2 = P_PointOnLineSide (m_Box[BOXRIGHT], m_Box[BOXBOTTOM], ld);
		break;
		
	case ST_NEGATIVE:
	default:	// Just to assure GCC that p1 and p2 really do get initialized
		p1 = P_PointOnLineSide (m_Box[BOXRIGHT], m_Box[BOXTOP], ld);
		p2 = P_PointOnLineSide (m_Box[BOXLEFT], m_Box[BOXBOTTOM], ld);
		break;
	}

	return (p1 == p2) ? p1 : -1;
}

