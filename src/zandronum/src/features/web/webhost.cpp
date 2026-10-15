// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The SDL layer's host in a browser (posix/sdl/sdlhost.h): the page's frame loop, its arguments, its canvas, its pointer lock and its error panel.

#include <emscripten.h>
#include <emscripten/html5.h>

#include "posix/sdl/sdlhost.h"
#include "features/web/webdriver.h"

namespace zx { namespace sdlhost {

// [rc4l] A browser has no signals to catch a crash with; the page catches one (web/crashreport.js).
void Start(int, char **, void (*terms)(), int (*)(char *, char *))
{
	zx::web::InstallDriver(terms);
}

void AddArgs(DArgs *args) { zx::web::AppendPageArgs(args); }

bool OwnsFullscreen() { return true; }

void SetFullscreen(bool on) { zx::web::SetPageFullscreen(on); }

bool MouseHeld()
{
	EmscriptenPointerlockChangeEvent lock;
	return emscripten_get_pointerlock_status(&lock) == EMSCRIPTEN_RESULT_SUCCESS && lock.isActive;
}

void ShowFatal(const char *text) { zx::web::Fatal(text); }

bool CanAskForIWad() { return false; }

// [rc4l] The page copies the canvas after the frame (Module.postMainLoop), the one moment it still holds it.
bool CaptureFrameLater(const char *path)
{
	return EM_ASM_INT({
		if (!Module.fuaCaptureFrame) return 0;
		Module.fuaCaptureFrame(UTF8ToString($0));
		return 1;
	}, path) != 0;
}

}} // namespace zx::sdlhost

// [rc4l] The page's animation frame paces the frames, and a browser has no POSIX timers, so there is nothing to limit (posix/sdl/i_fpslimit.cpp on a desktop).
void I_SetFPSLimit(int) {}
