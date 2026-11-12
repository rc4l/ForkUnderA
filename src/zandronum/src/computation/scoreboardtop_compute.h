// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Where the intermission's scoreboard starts below the level's title.

#ifndef ZX_SCOREBOARDTOP_COMPUTE_H
#define ZX_SCOREBOARDTOP_COMPUTE_H

namespace zx {

// [rc4l] Ported from zandronum@465e1c524db5cbbfdc2ae10b8e17fdcb41fe43e5: a title graphic of a whole 320x200 screen (Eviternity's) would push the board off the bottom, so it starts 32 clean pixels down instead; titleHeight is negative with no graphic.
int ComputeScoreboardTop(int minYPos, int cleanYfac, int titleHeight);

} // namespace zx

#endif
