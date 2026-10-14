// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/jobs/jobs.h"

#include "c_cvars.h"

#include <thread>

// [rc4l] Workers beside the game thread for a frame's parallel work: -1 picks by the machine, 0 runs everything on the game thread.
CVAR(Int, fua_jobs_threads, -1, 0)
// [rc4l] Microseconds an idle worker polls for the next job before it sleeps; a sleeping worker wakes late to a frame's next job.
CVAR(Int, fua_jobs_spin_us, 200, 0)

namespace zx { namespace jobs {

namespace {
// [rc4l] Never destroyed, so no worker is joined while the process exits.
JobPool *g_pool = NULL;
const int kMaxWorkers = 63;
}

JobPool &Pool()
{
	int want = fua_jobs_threads;
	if (want < 0) want = ComputeAutoWorkers((int)std::thread::hardware_concurrency(), 7);
	if (want > kMaxWorkers) want = kMaxWorkers;
	if (g_pool == NULL) g_pool = new JobPool(want);
	else if (want != g_pool->Workers()) g_pool->Resize(want);
	if (g_pool->SpinMicros() != fua_jobs_spin_us) g_pool->SetSpinMicros(fua_jobs_spin_us);
	return *g_pool;
}

}} // namespace zx::jobs
