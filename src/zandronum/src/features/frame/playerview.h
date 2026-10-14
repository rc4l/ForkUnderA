// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's view: the frame and viewpoint set-up GL's RenderView and RenderViewpoint share with a frame no GL renderer draws, and that frame itself.

#ifndef ZX_PLAYERVIEW_H
#define ZX_PLAYERVIEW_H

struct player_t;
class AActor;

namespace zx { namespace frame {

// [rc4l] Before any view of a frame, once whichever renderers draw it: a new camera's interpolation reset, the tic fraction and clock, the frame's serial, particles, last frame's camera textures, the fixed colormap, and the lens in r_viewLens.
void BeginPlayerFrame(player_t *player);

// [rc4l] Forget the last camera, so the next frame starts uninterpolated (a new frame buffer, a game restart).
void ForgetCamera();

// [rc4l] Before a viewpoint's walks: which view they belong to, the backend's lens for the player's, the frame set up from the camera, its view area and features/viewsetup's view; returns what EndViewpoint restores.
bool BeginViewpoint(AActor *camera, float fov, float fovRatio, bool mainview, bool toscreen);

// [rc4l] After it: the frame count, the interpolations restored, and the walks' view as it was.
void EndViewpoint(bool savedMainView);

// [rc4l] Only the map section the eye is in is drawn.
void MarkMapSection();

// [rc4l] The backend's frame, once BeginPlayerFrame has set it up: its views, the standalone scene, and the weapon, targeter and screen tint into the 2D capture; `paletteBrightness` darkens a colormap's tint.
void RenderPlayerView(player_t *player, int paletteBrightness);

}} // namespace zx::frame

#endif // ZX_PLAYERVIEW_H
