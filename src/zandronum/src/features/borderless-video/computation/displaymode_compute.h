// [rc4l] Display-mode decision logic for the borderless-windowed feature.
//
// Matches UZDoom/GZDoom's model: fullscreen means a borderless window at desktop resolution
// (exclusive fullscreen was removed upstream in b65b83edb). Our SDL2 backend (macOS/Linux)
// already does this via SDL_WINDOW_FULLSCREEN_DESKTOP; this feature brings the native Win32
// backend in line so "fullscreen" is borderless on every platform.
//
// This unit is the single, pure, tested source of truth for what the boolean `fullscreen`
// CVAR means for window construction. It is deliberately header-pure (no SDL/Win32/engine
// includes) so it is EASY TO REPLACE: when upstream's video backend is eventually cherry-picked
// wholesale, swap this unit + the two thin backend call sites marked "[rc4l] borderless-video"
// and nothing else changes. Backends ask this unit; they never branch on the raw CVAR.
//
// Provenance: GZDoom's win_borderless (938cd3cab, 2017-12-07) and the fullscreen-becomes-
// borderless keystone (b65b83edb, 2018-06-17). See docs/borderless-feasibility.md.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#ifndef ZX_DISPLAYMODE_COMPUTE_H
#define ZX_DISPLAYMODE_COMPUTE_H

namespace zx
{

// Backend-agnostic window intent. The SDL2/Win32 glue translates this to concrete flags/styles;
// it never inspects the CVAR directly. Two kinds only -- exclusive fullscreen is not offered
// (matching upstream).
enum WindowKind
{
	WINDOW_NORMAL,             // decorated, movable, sized by the caller (windowed)
	WINDOW_BORDERLESS_DESKTOP, // undecorated, covers the display at desktop resolution
};

// Map the boolean `fullscreen` CVAR to the window intent for the backend glue.
WindowKind WindowKindForFullscreen(bool fullscreen);

// The borderless window rectangle for a display with the given origin and size. Trivial today
// (cover the whole display), but isolated + tested so multi-monitor / work-area tweaks land here
// rather than in the Win32 glue. Writes the four out-params; all inputs in pixels.
void BorderlessWindowRect(int displayX, int displayY, int displayW, int displayH,
                          int *outX, int *outY, int *outW, int *outH);

} // namespace zx

#endif // ZX_DISPLAYMODE_COMPUTE_H
