// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's view for a frame no GL renderer draws: the engine half of FGLRenderer::RenderView, RenderViewpoint and EndDrawScene.

#ifndef ZX_PLAYERVIEW_H
#define ZX_PLAYERVIEW_H

struct player_t;

namespace zx { namespace frame {

// [rc4l] The frame's clocks and camera textures, the view set up, the standalone scene, and the weapon, targeter and screen tint into the 2D capture; `paletteBrightness` darkens a colormap's tint.
void RenderPlayerView(player_t *player, int paletteBrightness);

}} // namespace zx::frame

#endif // ZX_PLAYERVIEW_H
