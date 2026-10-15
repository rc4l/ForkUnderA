// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] What the operating system says about this process, each platform's half in a file of its own (osprobe_win32.cpp, osprobe_linux.cpp, osprobe_mac.cpp, osprobe_web.cpp) that CMake picks; a reading a platform cannot take stays negative.

#ifndef ZX_TELEMETRY_OSPROBE_H
#define ZX_TELEMETRY_OSPROBE_H

#include <string>
#include <vector>
#ifdef _WIN32
#include <mutex>
#endif

namespace zx { namespace telemetry {

struct OsReadings
{
	// [rc4l] Memory only this process holds: committed private bytes, anonymous pages, or the physical footprint.
	double privateBytes = -1.0;
	// [rc4l] Its pages in physical memory now, and the most there have been; a browser's wasm memory, which never shrinks.
	double residentBytes = -1.0;
	double peakBytes = -1.0;
	// [rc4l] The C heap's bytes handed out and not freed.
	double heapBytes = -1.0;
	// [rc4l] Open handles or file descriptors, and threads.
	double handles = -1.0;
	double threads = -1.0;
	// [rc4l] GPU memory the process holds as the OS counts it (DXGI on Windows) or the page does (every WebGPU buffer and texture made and not destroyed).
	double gpuProcessBytes = -1.0;
	// [rc4l] A browser's JavaScript heap, where it says.
	double jsHeapBytes = -1.0;
};

// [rc4l] Takes every reading this platform can; cheap enough for a few times a second.
void ReadOs(OsReadings &out);

// [rc4l] Which probe this build has, for fua_stats' header.
const char *OsProbeName();

// [rc4l] The calling thread's return addresses, innermost first, at most max of them, allocating nothing; 0 where the platform cannot walk its stack (a browser).
int CaptureStack(void **frames, int max);
// [rc4l] The function each address is in, with its source file's bare name and line where the platform knows them, empty for one it knows nothing of; the symbol tables are open for the call alone.
std::vector<std::string> SymbolNames(const std::vector<void *> &addresses);

#ifdef _WIN32
// [rc4l] Held around every dbghelp session but a crash's (mcp_sample.cpp's too), since dbghelp is one per process and not thread-safe.
std::mutex &DbgHelpLock();
#endif

}} // namespace zx::telemetry

#endif // ZX_TELEMETRY_OSPROBE_H
