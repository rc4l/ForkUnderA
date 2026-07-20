//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
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
//		Status bar code.
//		Does the face/direction indicator animatin.
//		Does palette indicators as well (red pain/berserk, bright pickup)
//
//-----------------------------------------------------------------------------

#ifndef __STSTUFF_H__
#define __STSTUFF_H__

struct event_t;

extern int ST_X;
extern int ST_Y;

bool ST_Responder(event_t* ev);

// [RH] Base blending values (for e.g. underwater)
extern int BaseBlendR, BaseBlendG, BaseBlendB;
extern float BaseBlendA;

#endif
