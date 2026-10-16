// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The browser's driver: the main loop once an animation frame, a restart as a page reload, and a fatal error shown on the page.

#ifndef ZX_WEBDRIVER_H
#define ZX_WEBDRIVER_H

class DArgs;

namespace zx { namespace web {

// [rc4l] Install the frame driver, before D_DoomMain, so D_DoomLoop hands its frames to the browser; `terms` is the engine's shutdown, which an exit runs since a page has no atexit.
void InstallDriver(void (*terms)());

// [rc4l] Stop the loop and show `message` on the page, in place of a desktop's message box and exit.
void Fatal(const char *message);

// [rc4l] The page's canvas fullscreen or back, which the browser allows only shortly after a key or click, as a menu's toggle is.
void SetPageFullscreen(bool on);

// [rc4l] The page's arguments: -offline, since a page has no UDP to play or browse with, then the URL's query (ComputeArgsFromQuery), so a link can name the game, the wads and the map.
void AppendPageArgs(DArgs *args);

}} // namespace zx::web

#endif // ZX_WEBDRIVER_H
