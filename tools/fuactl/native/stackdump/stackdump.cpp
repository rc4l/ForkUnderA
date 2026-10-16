// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] fuactl stacks' helper: the call stacks of a process's busiest threads, for an engine that hangs where its own sampler, which reports through the game thread, cannot answer.
//
//   stackdump <pid> [threads=3] [samples=1]

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

// [rc4l] A thread's CPU time so far, in 100 ns units.
static ULONGLONG CpuTime(HANDLE thread)
{
	FILETIME created, exited, kernel, user;
	if (!GetThreadTimes(thread, &created, &exited, &kernel, &user)) return 0;
	return ((ULONGLONG)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) + ((ULONGLONG)user.dwHighDateTime << 32 | user.dwLowDateTime);
}

struct Thread { DWORD id; HANDLE handle; ULONGLONG before, spent; };

// [rc4l] The process's threads, busiest over half a second first.
static std::vector<Thread> BusiestThreads(DWORD pid)
{
	std::vector<Thread> threads;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	THREADENTRY32 entry = { sizeof(entry) };
	for (BOOL ok = Thread32First(snap, &entry); ok; ok = Thread32Next(snap, &entry))
	{
		if (entry.th32OwnerProcessID != pid) continue;
		HANDLE h = OpenThread(THREAD_ALL_ACCESS, FALSE, entry.th32ThreadID);
		if (h != NULL) threads.push_back({ entry.th32ThreadID, h, CpuTime(h), 0 });
	}
	CloseHandle(snap);
	Sleep(500);
	for (Thread &t : threads) t.spent = CpuTime(t.handle) - t.before;
	std::sort(threads.begin(), threads.end(), [](const Thread &a, const Thread &b) { return a.spent > b.spent; });
	return threads;
}

// [rc4l] One thread's stack, suspended for the walk, each frame as module!symbol and its source line when the symbols have one.
static void PrintStack(HANDLE process, const Thread &t)
{
	SuspendThread(t.handle);
	CONTEXT ctx = {};
	ctx.ContextFlags = CONTEXT_FULL;
	if (GetThreadContext(t.handle, &ctx))
	{
		STACKFRAME64 frame = {};
		frame.AddrPC.Offset = ctx.Rip;    frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
		for (int depth = 0; depth < 48; depth++)
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, t.handle, &frame, &ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
			if (frame.AddrPC.Offset == 0) break;
			char buf[sizeof(SYMBOL_INFO) + 512] = {};
			SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
			sym->SizeOfStruct = sizeof(SYMBOL_INFO);
			sym->MaxNameLen = 511;
			IMAGEHLP_MODULE64 module = { sizeof(module) };
			SymGetModuleInfo64(process, frame.AddrPC.Offset, &module);
			IMAGEHLP_LINE64 line = { sizeof(line) };
			DWORD lineOffset = 0;
			const bool hasLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineOffset, &line) != FALSE;
			DWORD64 displacement = 0;
			if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, sym))
			{
				if (hasLine) printf("  %s!%s  %s:%lu\n", module.ModuleName, sym->Name, line.FileName, line.LineNumber);
				else printf("  %s!%s+0x%llx\n", module.ModuleName, sym->Name, (unsigned long long)displacement);
			}
			else printf("  %s!0x%llx\n", module.ModuleName, (unsigned long long)frame.AddrPC.Offset);
		}
	}
	ResumeThread(t.handle);
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: stackdump <pid> [threads] [samples]\n"); return 2; }
	const DWORD pid = (DWORD)atoi(argv[1]);
	const int top = argc > 2 ? atoi(argv[2]) : 3;
	const int samples = argc > 3 ? atoi(argv[3]) : 1;
	HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
	if (process == NULL) { fprintf(stderr, "cannot open process %lu (error %lu)\n", pid, GetLastError()); return 1; }
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	if (!SymInitialize(process, NULL, TRUE)) { fprintf(stderr, "cannot load symbols (error %lu)\n", GetLastError()); return 1; }
	const std::vector<Thread> threads = BusiestThreads(pid);
	for (int s = 0; s < samples; s++)
	{
		for (int i = 0; i < top && i < (int)threads.size(); i++)
		{
			printf("=== thread %lu, %.0f ms CPU in 500 ms (sample %d)\n", threads[i].id, threads[i].spent / 10000.0, s + 1);
			PrintStack(process, threads[i]);
		}
		if (s + 1 < samples) Sleep(100);
	}
	return 0;
}
