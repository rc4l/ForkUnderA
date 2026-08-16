// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Win32 window creation for the Diligent backend, isolated in its own translation unit.
//
// It is separate for a concrete build reason. `windows_build.ps1` reshapes the Windows SDK into
// `dxsdk/Include` (the engine needs DirectX 9 headers that ship nowhere else), and that directory is
// on the global include path -- so a plain `#include <windows.h>` in a file that also pulls Diligent
// headers resolves to the reshaped copies and collides with the real SDK: `DWORD` redefined,
// `_BitScanForward` "cannot overload a function with extern C linkage", and a hundred more.
//
// Keeping Win32 here (with the same WIN32_LEAN_AND_MEAN prologue every other win32/ file uses, which
// is what makes it resolve correctly) and handing the caller an opaque `void*` means the Diligent TU
// never includes windows.h at all. It also compiles at the engine's C++14 rather than the C++17 the
// Diligent headers force.

#ifdef FUA_DILIGENT

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#define _WIN32_IE 0x0500
#include <windows.h>

namespace zx { namespace hwrender {

static LRESULT CALLBACK DgWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
	// [rc4l] The console owns this window's lifetime, not its close button -- destroying it out from
	// under a live swapchain would take the device with it.
	if (msg == WM_CLOSE) return 0;
	return DefWindowProc(h, msg, w, l);
}

void *Fua_CreateBackendWindow(const char *title, int w, int h)
{
	static bool registered = false;
	if (!registered)
	{
		WNDCLASSEXA wc;
		memset(&wc, 0, sizeof(wc));
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = DgWndProc;
		wc.hInstance = GetModuleHandle(NULL);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.lpszClassName = "FuaDiligentWnd";
		if (!RegisterClassExA(&wc)) return NULL;
		registered = true;
	}

	// [rc4l] w/h are the CLIENT size, not the window size.
	//
	// CreateWindow takes the outer rect, so asking for 640x400 gave a 624x361 client area -- and the
	// backend then rendered the engine's 640x480 2D layer into it. The HUD came out squashed to 75%
	// height and point-sampled, which made small text unreadable: the "garbled HUD". Any comparison
	// against a GL screenshot was also being made between two different aspect ratios.
	const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	RECT rc;
	rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
	AdjustWindowRect(&rc, style, FALSE);

	HWND hwnd = CreateWindowExA(0, "FuaDiligentWnd", title,
		style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, GetModuleHandle(NULL), NULL);
	return (void *)hwnd;
}

// [rc4l] The engine's own message pump does not service this window, so it would otherwise show as
// "not responding" and never repaint. Called once per presented frame.
void Fua_PumpBackendWindow(void *hwnd)
{
	MSG msg;
	while (PeekMessage(&msg, (HWND)hwnd, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

}} // namespace zx::hwrender

#endif // FUA_DILIGENT
