// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The macOS probe: the task's VM info for its footprint, resident size and peak, its thread list, its descriptor count, and the default malloc zones' statistics.

#include "features/telemetry/osprobe.h"

#include <libproc.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#include <unistd.h>

namespace zx { namespace telemetry {

void ReadOs(OsReadings &out, bool walkHeaps)
{
	task_vm_info_data_t vm = {};
	mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
	if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) == KERN_SUCCESS)
	{
		out.privateBytes = (double)vm.phys_footprint;
		out.residentBytes = (double)vm.resident_size;
		out.peakBytes = (double)vm.resident_size_peak;
	}
	thread_act_array_t threads = NULL;
	mach_msg_type_number_t threadCount = 0;
	if (task_threads(mach_task_self(), &threads, &threadCount) == KERN_SUCCESS)
	{
		out.threads = (double)threadCount;
		for (mach_msg_type_number_t i = 0; i < threadCount; i++) mach_port_deallocate(mach_task_self(), threads[i]);
		vm_deallocate(mach_task_self(), (vm_address_t)threads, threadCount * sizeof(thread_t));
	}
	const int fdBytes = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, NULL, 0);
	if (fdBytes > 0) out.handles = (double)(fdBytes / PROC_PIDLISTFD_SIZE);
	if (!walkHeaps) return;
	malloc_statistics_t heap = {};
	malloc_zone_statistics(NULL, &heap);
	out.heapBytes = (double)heap.size_in_use;
}

const char *OsProbeName()
{
	return "mac";
}

}} // namespace zx::telemetry
