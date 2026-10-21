// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the compositor actually put on screen of one window, frame by frame, through DXGI Desktop Duplication.
//
// usage: screencap <pid> <seconds> <out.csv> [window class]
// Prints one JSON line (the refresh rate, the patch watched, the start on the QPC clock in microseconds, and whether
// another window covered the patch's centre at the start or the end, when the capture shows that window); each CSV
// row is one composed desktop frame: when it was presented (QPC microseconds from the start), whether the window's
// centre patch changed, and how many desktop frames it stands for (more than one when the capture fell behind).

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

namespace {

struct Find { DWORD pid; HWND best; LONG area; const char *cls; };

// [rc4l] Whether the window at screen point (x, y) belongs to some other top-level window than `own`.
int Covered(HWND own, int x, int y)
{
	POINT p = { x, y };
	HWND at = WindowFromPoint(p);
	return at != NULL && GetAncestor(at, GA_ROOT) != own ? 1 : 0;
}

BOOL CALLBACK EnumProc(HWND h, LPARAM lp)
{
	Find *f = (Find *)lp;
	DWORD pid = 0;
	GetWindowThreadProcessId(h, &pid);
	if (pid != f->pid || !IsWindowVisible(h)) return TRUE;
	char cls[128] = "";
	GetClassNameA(h, cls, sizeof(cls));
	if (f->cls != NULL && strcmp(cls, f->cls) != 0) return TRUE;
	RECT r;
	GetClientRect(h, &r);
	const LONG area = (r.right - r.left) * (r.bottom - r.top);
	if (area > f->area) { f->area = area; f->best = h; }
	return TRUE;
}

// [rc4l] FNV-1a over every other texel of every other row: enough to tell one picture from the next.
uint64_t PatchHash(const D3D11_MAPPED_SUBRESOURCE &m, int w, int h)
{
	uint64_t hash = 1469598103934665603ULL;
	for (int y = 0; y < h; y += 2)
	{
		const uint32_t *row = (const uint32_t *)((const uint8_t *)m.pData + y * m.RowPitch);
		for (int x = 0; x < w; x += 2) { hash ^= row[x]; hash *= 1099511628211ULL; }
	}
	return hash;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: screencap <pid> <seconds> <out.csv> [window class]\n"); return 2; }
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	Find f = { (DWORD)atoi(argv[1]), NULL, 0, argc > 4 ? argv[4] : NULL };
	EnumWindows(EnumProc, (LPARAM)&f);
	if (f.best == NULL) { fprintf(stderr, "no visible window for pid %s\n", argv[1]); return 1; }
	RECT cr;
	GetClientRect(f.best, &cr);
	POINT tl = { 0, 0 };
	ClientToScreen(f.best, &tl);
	const int pw = 256, ph = 256;
	const int px = tl.x + (cr.right - cr.left) / 2 - pw / 2, py = tl.y + (cr.bottom - cr.top) / 2 - ph / 2;

	ID3D11Device *dev = NULL;
	ID3D11DeviceContext *ctx = NULL;
	D3D_FEATURE_LEVEL fl;
	if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) { fprintf(stderr, "no d3d11 device\n"); return 1; }
	IDXGIDevice *xdev = NULL;
	dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&xdev);
	IDXGIAdapter *adapter = NULL;
	xdev->GetAdapter(&adapter);
	IDXGIOutput *out = NULL;
	DXGI_OUTPUT_DESC od;
	for (UINT i = 0; adapter->EnumOutputs(i, &out) != DXGI_ERROR_NOT_FOUND; i++)
	{
		out->GetDesc(&od);
		if (px >= od.DesktopCoordinates.left && px < od.DesktopCoordinates.right && py >= od.DesktopCoordinates.top && py < od.DesktopCoordinates.bottom) break;
		out->Release();
		out = NULL;
	}
	if (out == NULL) { fprintf(stderr, "the window is on no output of the default adapter\n"); return 1; }
	DEVMODEW dm = {};
	dm.dmSize = sizeof(dm);
	EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &dm);
	IDXGIOutput1 *out1 = NULL;
	out->QueryInterface(__uuidof(IDXGIOutput1), (void **)&out1);
	IDXGIOutputDuplication *dup = NULL;
	if (FAILED(out1->DuplicateOutput(dev, &dup))) { fprintf(stderr, "DuplicateOutput failed\n"); return 1; }

	D3D11_TEXTURE2D_DESC sd = {};
	sd.Width = pw; sd.Height = ph; sd.MipLevels = 1; sd.ArraySize = 1; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_STAGING; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D *staging = NULL;
	dev->CreateTexture2D(&sd, NULL, &staging);

	LARGE_INTEGER freq, start, now;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);
	// [rc4l] As std::chrono::steady_clock counts it, so an engine's own timestamps line up with these.
	const long long startUs = (start.QuadPart / freq.QuadPart) * 1000000 + (start.QuadPart % freq.QuadPart) * 1000000 / freq.QuadPart;
	const int coveredAtStart = Covered(f.best, px + pw / 2, py + ph / 2);
	const double seconds = atof(argv[2]);
	FILE *csv = fopen(argv[3], "w");
	if (csv == NULL) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
	fprintf(csv, "present_us,changed,frames\n");
	uint64_t last = 0;
	const int sx = px - od.DesktopCoordinates.left, sy = py - od.DesktopCoordinates.top;
	for (;;)
	{
		QueryPerformanceCounter(&now);
		if ((now.QuadPart - start.QuadPart) > (LONGLONG)(seconds * freq.QuadPart)) break;
		DXGI_OUTDUPL_FRAME_INFO fi;
		IDXGIResource *res = NULL;
		const HRESULT hr = dup->AcquireNextFrame(50, &fi, &res);
		if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
		if (FAILED(hr)) { fprintf(stderr, "AcquireNextFrame 0x%08x\n", (unsigned)hr); break; }
		if (fi.LastPresentTime.QuadPart != 0)
		{
			ID3D11Texture2D *tex = NULL;
			res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
			D3D11_BOX box = { (UINT)sx, (UINT)sy, 0, (UINT)(sx + pw), (UINT)(sy + ph), 1 };
			ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, tex, 0, &box);
			D3D11_MAPPED_SUBRESOURCE m;
			uint64_t hash = last;
			if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) { hash = PatchHash(m, pw, ph); ctx->Unmap(staging, 0); }
			fprintf(csv, "%lld,%d,%u\n", (long long)((fi.LastPresentTime.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart), hash != last ? 1 : 0,
				fi.AccumulatedFrames);
			last = hash;
			tex->Release();
		}
		res->Release();
		dup->ReleaseFrame();
	}
	fclose(csv);
	const int covered = coveredAtStart | Covered(f.best, px + pw / 2, py + ph / 2);
	printf("{\"refreshHz\":%lu,\"patch\":[%d,%d,%d,%d],\"startUs\":%lld,\"covered\":%d}\n", dm.dmDisplayFrequency, px, py, pw, ph, startUs, covered);
	fflush(stdout);
	return 0;
}
