// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The player's view: the frame and viewpoint set-up GL's RenderView and RenderViewpoint share with a frame no GL renderer draws, and that frame itself.

#ifndef ZX_PLAYERVIEW_H
#define ZX_PLAYERVIEW_H

struct FViewLens;

namespace zx { namespace frame {

// [rc4l] Before any view of a frame, once whichever renderers draw it: a new camera's interpolation reset, the tic fraction and clock, the frame's serial, particles, last frame's camera textures, the fixed colormap, and the lens in r_viewLens.
void BeginPlayerFrame(int player);

// [rc4l] The clock the frame's animated textures and sky scroll read: fua_debug_frameclock when a capture pins it, else `nowMs`.
unsigned int FrameClock(unsigned int nowMs);


// [rc4l] Before a viewpoint's walks: which view they belong to, the frame set up from the render copy's record of the camera, its view area and features/viewsetup's view through `lens`; returns what EndViewpoint restores (GL's own BeginGLViewpoint takes the live camera).
bool BeginRecordViewpoint(int record, const FViewLens &lens, bool mainview, bool toscreen);
// [rc4l] Their shared tail once the frame is set up: the view area and features/viewsetup's view.
void SetViewpointUp(const FViewLens &lens, bool mainview, bool toscreen);

// [rc4l] After it: the frame count, the live game's interpolations restored when the view moved it, and the walks' view as it was.
void EndViewpoint(bool savedMainView);

// [rc4l] Only the map section the eye is in is drawn.
void MarkMapSection();

// [rc4l] The backend's frame, once BeginPlayerFrame has set it up: its views, the standalone scene, and the weapon, targeter and screen tint into the 2D capture; `paletteBrightness` darkens a colormap's tint.
void RenderPlayerView(int player, int paletteBrightness);

}} // namespace zx::frame

#endif // ZX_PLAYERVIEW_H
