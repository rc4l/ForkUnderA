// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The Linux probe: /proc/self/status for memory and threads, /proc/self/fd for descriptors, and glibc's heap statistics.

#include "features/telemetry/osprobe.h"
#include "features/telemetry/computation/telemetry_compute.h"
#include "features/fileio/computation/readfile_compute.h"

#include <dirent.h>
#include <malloc.h>
#include <string>

namespace zx { namespace telemetry {

namespace {

// [rc4l] The heap's bytes in use: small blocks plus mapped ones, through the call glibc 2.33 made wide enough for heaps past 2 GB.
double HeapInUse()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
	const struct mallinfo2 mi = mallinfo2();
#else
	const struct mallinfo mi = mallinfo();
#endif
	return (double)mi.uordblks + (double)mi.hblkhd;
}

} // namespace

void ReadOs(OsReadings &out)
{
	const std::string status = zx::ComputeFileText("/proc/self/status");
	out.privateBytes = ComputeStatusField(status, "RssAnon");
	out.residentBytes = ComputeStatusField(status, "VmRSS");
	out.peakBytes = ComputeStatusField(status, "VmHWM");
	out.threads = ComputeStatusField(status, "Threads");
	if (DIR *fds = opendir("/proc/self/fd"))
	{
		int n = 0;
		while (struct dirent *e = readdir(fds)) if (e->d_name[0] != '.') n++;
		closedir(fds);
		out.handles = n - 1.0;
	}
	out.heapBytes = HeapInUse();
}

const char *OsProbeName()
{
	return "linux";
}

}} // namespace zx::telemetry
