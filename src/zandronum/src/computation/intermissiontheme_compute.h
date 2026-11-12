// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which winner or loser theme a Skulltag intermission plays.

#ifndef ZX_INTERMISSIONTHEME_COMPUTE_H
#define ZX_INTERMISSIONTHEME_COMPUTE_H

namespace zx {

// [rc4l] Adapted from zandronum@59b494f63d585a9c5067c77ff228bb7fe2994cc0: the track of the player's team's theme when the team names one, else the map's.
int ComputeIntermissionThemeOrder(bool teamHasTheme, int teamOrder, int mapOrder);

// [rc4l] Whether the chosen theme plays: a team's always, the map's only when its lump is found, so a missing default falls back to the ordinary intermission music.
bool ComputePlaysIntermissionTheme(bool isMapTheme, bool lumpFound);

} // namespace zx

#endif
