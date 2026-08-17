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

// [rc4l] The engine's own window, declared at GLOBAL scope. Inside the namespace this names
// zx::hwrender::Window, which nothing defines, and only the linker notices.
extern HWND Window;

// [rc4l] Defined in dgscene.cpp, where the cvars live. Declared rather than included because this TU
// deliberately never sees a Diligent header.
namespace zx { namespace hwrender { bool Fua_WantEmbeddedWindow(); }}
using zx::hwrender::Fua_WantEmbeddedWindow;

namespace zx { namespace hwrender {

static LRESULT CALLBACK DgWndProc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
	// [rc4l] The console owns this window's lifetime, not its close button -- destroying it out from
	// under a live swapchain would take the device with it.
	if (msg == WM_CLOSE) return 0;
	return DefWindowProc(h, msg, w, l);
}

// [rc4l] Keep the embedded surface exactly over the engine's client area.
//
// The engine can change video mode at runtime, and a child that kept its creation-time size would
// leave a strip of the GL window showing down one edge -- which reads as a rendering bug in the
// backend rather than a window that is the wrong size.
void Fua_SyncBackendWindowToParent(void *hwnd)
{
	HWND h = (HWND)hwnd;
	if (h == NULL || GetParent(h) == NULL) return;
	RECT cr;
	if (!GetClientRect(GetParent(h), &cr)) return;
	RECT own;
	if (GetClientRect(h, &own) &&
		own.right == cr.right && own.bottom == cr.bottom) return;
	SetWindowPos(h, HWND_TOP, 0, 0, cr.right, cr.bottom, SWP_NOACTIVATE);
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
	// [rc4l] Render into the ENGINE's window when there is one, as a disabled child covering its
	// client area.
	//
	// The backend used to open a window of its own, which made it a spectator: the only way to move
	// was to drive the game window and watch this one beside it, and the moment you clicked the thing
	// you were looking at, the keyboard went with it. Embedding fixes that without any input plumbing
	// at all. WS_DISABLED means the child never takes mouse or keyboard -- Windows routes both to the
	// parent -- so the engine's existing input path, mouse capture and focus handling all keep working
	// untouched, while the only pixels on screen are the backend's. GL still draws into the parent
	// underneath, unseen, which is what keeps the wall cache and the draw lists being fed.
	//
	// This is also the first half of taking the window over properly: what is left is stopping the GL
	// frame underneath, which cannot happen until the backend covers portals and 3D floors.
	if (Window != NULL && Fua_WantEmbeddedWindow())
	{
		RECT cr;
		GetClientRect(Window, &cr);
		HWND child = CreateWindowExA(0, "FuaDiligentWnd", title,
			WS_CHILD | WS_VISIBLE | WS_DISABLED,
			0, 0, cr.right, cr.bottom,
			Window, NULL, GetModuleHandle(NULL), NULL);
		if (child != NULL) return (void *)child;
		// Fall through to a standalone window if the child could not be made.
	}

	const DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	RECT rc;
	rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
	AdjustWindowRect(&rc, style, FALSE);

	// [rc4l] WS_EX_NOACTIVATE: this window is a viewport, never an input target.
	//
	// The backend renders the same camera as the engine's own window, so the way to actually play in
	// Vulkan today is to drive the game window and watch this one beside it. Without NOACTIVATE that
	// falls apart the moment it appears -- it takes the foreground on creation, and a click anywhere
	// on it steals focus from the window that owns the keyboard, so the player stops moving and does
	// not obviously know why. With it, this window can be clicked, dragged and resized while input
	// stays where it belongs.
	//
	// It is also what keeps a live backend window out of the way of a HANDS-OFF harness run, where
	// stealing the foreground mid-measurement is worse than merely confusing.
	HWND hwnd = CreateWindowExA(WS_EX_NOACTIVATE, "FuaDiligentWnd", title,
		style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
		NULL, NULL, GetModuleHandle(NULL), NULL);
	if (hwnd != NULL)
	{
		// Beside the engine's window rather than on top of it: the whole point is seeing both at once.
		HWND fg = GetForegroundWindow();
		RECT fr;
		if (fg != NULL && fg != hwnd && GetWindowRect(fg, &fr))
		{
			SetWindowPos(hwnd, HWND_TOP, fr.right + 8, fr.top,
				0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}
	return (void *)hwnd;
}

// [rc4l] Show or hide the embedded surface, so toggling the backend off is a real A/B.
//
// The child covers the engine's client area, so with it visible the GL frame underneath can never be
// seen -- turning the backend off would freeze the last Vulkan frame on screen rather than reveal
// what GL is drawing, which is the one comparison worth being able to make instantly. Hiding it
// uncovers the parent, and GL has been rendering there the whole time.
void Fua_ShowBackendWindow(void *hwnd, int visible)
{
	HWND h = (HWND)hwnd;
	if (h == NULL || GetParent(h) == NULL) return;
	if ((IsWindowVisible(h) != 0) == (visible != 0)) return;
	ShowWindow(h, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
	if (!visible) InvalidateRect(GetParent(h), NULL, TRUE);
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
