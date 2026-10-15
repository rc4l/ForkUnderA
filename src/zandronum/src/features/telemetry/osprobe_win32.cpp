// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The Windows probe: the process's memory counters, its handle count, the process heap's summary, and the video memory DXGI says this process holds on each adapter, whatever API allocated it.

#include "features/telemetry/osprobe.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <dxgi1_4.h>
#pragma comment(lib, "dbghelp.lib")

namespace zx { namespace telemetry {

namespace {

// [rc4l] HeapSummary's record, declared here since the SDK hides it below Windows 10 targets.
struct HeapSummaryRecord { DWORD cb; SIZE_T cbAllocated, cbCommitted, cbReserved, cbMaxReserve; };
typedef BOOL (WINAPI *HeapSummaryFn)(HANDLE, DWORD, HeapSummaryRecord *);
typedef BOOL (WINAPI *MemoryInfoFn)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
typedef HRESULT (WINAPI *CreateFactoryFn)(REFIID, void **);

// [rc4l] A kernel32 or dxgi export, looked up once, so the probe links against nothing new.
template <class F> F Export(const char *module, const char *name)
{
	HMODULE m = GetModuleHandleA(module);
	if (m == NULL) m = LoadLibraryA(module);
	return m != NULL ? (F)(void *)GetProcAddress(m, name) : NULL;
}

// [rc4l] The local video memory this process holds summed over every adapter, or -1 without DXGI 1.4.
double DxgiProcessBytes()
{
	static IDXGIFactory1 *factory = NULL;
	static bool tried = false;
	if (!tried)
	{
		tried = true;
		const CreateFactoryFn create = Export<CreateFactoryFn>("dxgi.dll", "CreateDXGIFactory1");
		if (create != NULL) create(__uuidof(IDXGIFactory1), (void **)&factory);
	}
	if (factory == NULL) return -1.0;
	double total = 0.0;
	bool any = false;
	IDXGIAdapter1 *adapter = NULL;
	for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++)
	{
		IDXGIAdapter3 *adapter3 = NULL;
		if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), (void **)&adapter3)))
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
			if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
			{
				total += (double)info.CurrentUsage;
				any = true;
			}
			adapter3->Release();
		}
		adapter->Release();
	}
	return any ? total : -1.0;
}

// [rc4l] An address's function from the engine's .pdb, with its source file and line, inside an open dbghelp session.
std::string SymbolName(HANDLE proc, void *address)
{
	char buf[sizeof(SYMBOL_INFO) + 512];
	SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen = 511;
	DWORD64 disp = 0;
	if (!SymFromAddr(proc, (DWORD64)address, &disp, sym)) return std::string();
	std::string out = sym->Name;
	IMAGEHLP_LINE64 line = {};
	line.SizeOfStruct = sizeof(line);
	DWORD lineDisp = 0;
	if (SymGetLineFromAddr64(proc, (DWORD64)address, &lineDisp, &line) && line.FileName != NULL)
	{
		const char *file = line.FileName;
		for (const char *c = line.FileName; *c != 0; c++) if (*c == '\\' || *c == '/') file = c + 1;
		out += " (" + std::string(file) + ":" + std::to_string(line.LineNumber) + ")";
	}
	return out;
}

} // namespace

void ReadOs(OsReadings &out)
{
	static const MemoryInfoFn memoryInfo = Export<MemoryInfoFn>("kernel32.dll", "K32GetProcessMemoryInfo");
	static const HeapSummaryFn heapSummary = Export<HeapSummaryFn>("kernel32.dll", "HeapSummary");
	PROCESS_MEMORY_COUNTERS_EX pmc = {};
	pmc.cb = sizeof(pmc);
	if (memoryInfo != NULL && memoryInfo(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc)))
	{
		out.privateBytes = (double)pmc.PrivateUsage;
		out.residentBytes = (double)pmc.WorkingSetSize;
		out.peakBytes = (double)pmc.PeakWorkingSetSize;
	}
	DWORD handles = 0;
	if (GetProcessHandleCount(GetCurrentProcess(), &handles)) out.handles = (double)handles;
	HeapSummaryRecord heap = {};
	heap.cb = sizeof(heap);
	if (heapSummary != NULL && heapSummary(GetProcessHeap(), 0, &heap)) out.heapBytes = (double)heap.cbAllocated;
	out.gpuProcessBytes = DxgiProcessBytes();
}

const char *OsProbeName()
{
	return "win32";
}

int CaptureStack(void **frames, int max)
{
	return (int)RtlCaptureStackBackTrace(0, (DWORD)max, frames, NULL);
}

std::mutex &DbgHelpLock()
{
	static std::mutex lock;
	return lock;
}

// [rc4l] A session of its own each call, since the bridge's sampler opens and closes dbghelp around its own.
std::vector<std::string> SymbolNames(const std::vector<void *> &addresses)
{
	std::lock_guard<std::mutex> hold(DbgHelpLock());
	HANDLE proc = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	const bool open = SymInitialize(proc, NULL, TRUE) != FALSE;
	std::vector<std::string> names;
	for (void *address : addresses) names.push_back(open ? SymbolName(proc, address) : std::string());
	if (open) SymCleanup(proc);
	return names;
}

}} // namespace zx::telemetry
