// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The level's HUD recorded in the handoff, where it may read the game, and replayed by every frame after, for a frame GL does not draw.

#ifndef ZX_GAMELOOP_HUD_H
#define ZX_GAMELOOP_HUD_H

#include "features/gameloop/computation/hudrecord_compute.h"

namespace zx { namespace gameloop {

// [rc4l] Records the HUD `draw` draws when a frame without GL will show it and the recording is stale under `now`; forgets it when GL draws or no HUD shows.
void RecordHUD(bool shows, const HudKey &now, void (*draw)());
// [rc4l] Replays the recording into this frame's 2D, false when there is none and the frame draws the HUD itself.
bool ReplayHUD();

}} // namespace zx::gameloop

#endif // ZX_HANDOFF_HUD_H
