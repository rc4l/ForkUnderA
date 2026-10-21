// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The display's refresh as Windows and SDL report it; macOS asks in displayrefresh_cocoa.mm, and a browser's loop reads its frames instead (dynres.cpp).

#include "features/dynres/dynres.h"

// [rc4l] macOS compiles displayrefresh_cocoa.mm instead, beside this file in an SDL build there.
#if !defined(__APPLE__)

#if defined(_WIN32)
#include <windows.h>
extern HWND Window;
#else
#include <SDL.h>
#endif

namespace zx { namespace dynres {

double DisplayRefreshHz()
{
#if defined(_WIN32)
	HMONITOR monitor = MonitorFromWindow(Window, Window != NULL ? MONITOR_DEFAULTTONEAREST : MONITOR_DEFAULTTOPRIMARY);
	MONITORINFOEXW info;
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info)) return 0.0;
	DEVMODEW mode = {};
	mode.dmSize = sizeof(mode);
	if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) return 0.0;
	// [rc4l] Windows reports 0 or 1 for "the hardware's default", which is no rate at all.
	return mode.dmDisplayFrequency > 1 ? (double)mode.dmDisplayFrequency : 0.0;
#else
	SDL_DisplayMode mode;
	if (SDL_GetCurrentDisplayMode(0, &mode) != 0) return 0.0;
	return mode.refresh_rate > 0 ? (double)mode.refresh_rate : 0.0;
#endif
}

}} // namespace zx::dynres

#endif // !__APPLE__
