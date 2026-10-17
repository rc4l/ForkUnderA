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
//		?
//
//-----------------------------------------------------------------------------


#ifndef __P_TICK__
#define __P_TICK__



// Called by C_Ticker,
// can call G_PlayerExited.
// Carries out all thinking of monsters and players.
void P_Ticker (void);

// [rc4l] features/gameloop: r_NoInterpolate as P_Ticker leaves it, handed to the renderer between tics, since the tic may run beside a frame.
extern bool p_NoInterpolate;

bool P_CheckTickerPaused ();
// [rc4l] features/gameloop: whether the tic is paused for a demo, a menu or the console, without pausing the sound as P_CheckTickerPaused does.
bool P_TickerPausedNow ();


#endif
