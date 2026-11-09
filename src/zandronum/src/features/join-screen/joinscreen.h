// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Doruk-Aksoy
//
// [Doruk-Aksoy] A loading screen over the window while joining a server: the mod's title page, its GAMEINFO startup banner, a status line and a bar.
#ifndef ZX_JOINSCREEN_H
#define ZX_JOINSCREEN_H

namespace zx
{

// [Doruk-Aksoy] Draws the screen when a join is under way and the player has not asked for the console; true when it drew.
bool JoinScreen_Draw();

// [Doruk-Aksoy] The console key during a join swaps between this screen and the console; true when a join screen took the key.
bool JoinScreen_TogglePeek();

} // namespace zx

#endif // ZX_JOINSCREEN_H
