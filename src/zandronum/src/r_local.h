//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Marisa Heit
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
//		Refresh (R_*) module, global header.
//		All the rendering/drawing stuff is here.
//
//-----------------------------------------------------------------------------

#ifndef __R_LOCAL_H__
#define __R_LOCAL_H__

// Binary Angles, sine/cosine/atan lookups.
#include "tables.h"

// Screen size related parameters.
#include "doomdef.h"

// Include the refresh/render data structs.

//
// Separate header file for each module.
//
#include "r_main.h"
// [rc4l] r_things.h / r_draw.h removed with the software renderer (GL-only build).

#endif // __R_LOCAL_H__
