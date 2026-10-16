// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the SDL layer's host does that differs between a desktop and a browser page: sdlhost_desktop.cpp on a desktop and features/web/webhost.cpp in a browser, chosen by CMake, so the SDL files hold no browser conditions.

#ifndef ZX_SDLHOST_H
#define ZX_SDLHOST_H

class DArgs;

namespace zx { namespace sdlhost {

// [rc4l] First in main: a desktop installs its crash catcher, reporting with crashInfo; a page hands its frames to the browser, which runs terms when it quits.
void Start(int argc, char **argv, void (*terms)(), int (*crashInfo)(char *, char *));

// [rc4l] Arguments the host adds after the command line's: none on a desktop, a page's own (features/web's AppendPageArgs).
void AddArgs(DArgs *args);

// [rc4l] Whether the host owns fullscreen: a page makes its one canvas fullscreen, where a desktop rebuilds its SDL window.
bool OwnsFullscreen();

// [rc4l] Fullscreen on or off by a host that owns it.
void SetFullscreen(bool on);

// [rc4l] Whether the window holds the mouse: always on a desktop, while a page's pointer lock goes when Escape drops it.
bool MouseHeld();

// [rc4l] A fatal error shown where the player sees it before the exit: a page stops its loop and shows it, a desktop leaves it to stderr.
void ShowFatal(const char *text);

// [rc4l] Whether the player can be asked in a dialog which IWAD to play, which a page has none of.
bool CanAskForIWad();

// [rc4l] Whether the host saves this frame's picture as a PNG named like path once it is drawn: a page's canvas holds the frame only until its task ends, so the page copies it after the frame, where a desktop reads it back at once.
bool CaptureFrameLater(const char *path);

}} // namespace zx::sdlhost

#endif // ZX_SDLHOST_H
