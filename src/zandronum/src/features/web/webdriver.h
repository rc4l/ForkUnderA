// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The browser's driver: the main loop once an animation frame, a restart as a page reload, and a fatal error shown on the page.

#ifndef ZX_WEBDRIVER_H
#define ZX_WEBDRIVER_H

class DArgs;

namespace zx { namespace web {

// [rc4l] Install the frame driver, before D_DoomMain, so D_DoomLoop hands its frames to the browser.
void InstallDriver();

// [rc4l] Stop the loop and show `message` on the page, in place of a desktop's message box and exit.
void Fatal(const char *message);

// [rc4l] Add the page URL's query to the command line (ComputeArgsFromQuery), so a link can name the game, the wads and the map.
void AppendQueryArgs(DArgs *args);

}} // namespace zx::web

#endif // ZX_WEBDRIVER_H
