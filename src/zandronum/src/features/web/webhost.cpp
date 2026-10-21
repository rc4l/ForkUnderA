// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The SDL layer's host in a browser (posix/sdl/sdlhost.h): the page's frame loop, its arguments, its canvas, its pointer lock and its error panel.

#include <emscripten.h>
#include <emscripten/html5.h>

#include "posix/sdl/sdlhost.h"
#include "features/web/webdriver.h"
#include "features/telemetry/telemetry.h"

namespace zx { namespace sdlhost {

// [rc4l] A file the engine wrote for the player, such as a telemetry capture, leaves a browser only as a download from the page.
static void OfferFile(const char *path)
{
	EM_ASM({ if (Module.fuaOfferFile) Module.fuaOfferFile(UTF8ToString($0)); }, path);
}

// [rc4l] A browser has no signals to catch a crash with; the page catches one (web/crashreport.js).
void Start(int, char **, void (*terms)(), int (*)(char *, char *))
{
	zx::web::InstallDriver(terms);
	zx::telemetry::SetFileOfferer(OfferFile);
}

void AddArgs(DArgs *args) { zx::web::AppendPageArgs(args); }

bool OwnsFullscreen() { return true; }

void SetFullscreen(bool on) { zx::web::SetPageFullscreen(on); }

bool MouseHeld()
{
	EmscriptenPointerlockChangeEvent lock;
	return emscripten_get_pointerlock_status(&lock) == EMSCRIPTEN_RESULT_SUCCESS && lock.isActive;
}

// [rc4l] The page's pointers (web/touch.js keeps Module.fuaTouch: pointers as id, x, y, ox, oy, ended; the view's size and insets), copied in one call into a scratch of 6 floats a pointer after 7 of the view, the ended ones forgotten by the page once read.
bool ReadTouch(TouchPointer *out, int max, int &count, TouchView &view)
{
	static float scratch[7 + 6 * 16];
	const int cap = max < 16 ? max : 16;
	// [rc4l] No comma outside parentheses in the script: the macro splits its arguments on them.
	const int n = EM_ASM_INT({
		const t = Module.fuaTouch;
		if (!t || !t.active) return -1;
		const base = $0 >> 2;
		const cap = $1;
		HEAPF32[base] = t.width;
		HEAPF32[base + 1] = t.height;
		for (let i = 0; i < 4; i++) HEAPF32[base + 2 + i] = t.insets[i];
		HEAPF32[base + 6] = t.portrait ? 1 : 0;
		const list = t.pointers.slice(0, cap);
		for (let i = 0; i < list.length; i++) {
			const p = list[i];
			const at = base + 7 + i * 6;
			HEAPF32[at] = p.id;
			HEAPF32[at + 1] = p.x;
			HEAPF32[at + 2] = p.y;
			HEAPF32[at + 3] = p.ox;
			HEAPF32[at + 4] = p.oy;
			HEAPF32[at + 5] = p.ended ? 1 : 0;
		}
		t.pointers = t.pointers.filter((p) => !p.ended);
		return list.length;
	}, scratch, cap);
	count = 0;
	if (n < 0) return false;
	view.width = (int)scratch[0]; view.height = (int)scratch[1];
	view.insetLeft = scratch[2]; view.insetTop = scratch[3]; view.insetRight = scratch[4]; view.insetBottom = scratch[5];
	view.portrait = scratch[6] != 0.f;
	for (int i = 0; i < n; i++)
	{
		const float *p = scratch + 7 + i * 6;
		out[i].id = (int)p[0]; out[i].x = p[1]; out[i].y = p[2]; out[i].ox = p[3]; out[i].oy = p[4]; out[i].ended = p[5] != 0.f;
	}
	count = n;
	return true;
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
