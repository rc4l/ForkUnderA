// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/jobs/jobs.h"

#include "c_cvars.h"
#include "features/mainloop/mainloop.h"

#include <cstdint>
#include <cstdio>
#include <thread>
#include "features/telemetry/telemetry.h"

// [rc4l] Workers beside the main thread for a frame's parallel work: -1 picks by the machine, 0 runs everything on the main thread.
CVAR(Int, fua_jobs_threads, -1, 0)
EXTERN_CVAR(Bool, fua_gamethread)   // [rc4l] features/handoff: the game thread runs beside the pool's workers
// [rc4l] Microseconds an idle worker polls for the next job before it sleeps; a sleeping worker wakes late to a frame's next job.
CVAR(Int, fua_jobs_spin_us, 200, 0)
// [Doruk-Aksoy] Workers beside the game thread for the reads a tic splits across threads, as the render copy's scan at its end: -1 picks by the machine, 0 runs them on the game thread alone.
CVAR(Int, fua_jobs_tic_threads, -1, 0)

namespace zx { namespace jobs {

namespace {
// [rc4l] Never destroyed, so no worker is joined while the process exits.
JobPool *g_pool = NULL;
JobPool *g_ticPool = NULL;
const int kMaxWorkers = 63;
// [Doruk-Aksoy] The game thread's pool at most, since it works only at a tic's end, beside the frames' pool.
const int kTicWorkersCap = 3;

// [Doruk-Aksoy] What a pool is called in features/telemetry: its workers in traces, its lanes' runs summed and its caller's wait for the rest.
struct PoolNames { const char *worker, *run, *wait; };
const PoolNames kNames[] = { { "job", "jobs/run", "jobs/wait" }, { "tic job", "jobs/tic_run", "jobs/tic_wait" } };

// [rc4l] A pool's work in features/telemetry: each worker named in traces, every lane's runs summed, and the caller's wait for the rest.
template <int P> void WorkerStarted(int lane)
{
	char name[24];
	std::snprintf(name, sizeof(name), "%s %d", kNames[P].worker, lane);
	zx::telemetry::NameThisThread(name);
}

template <int P> void RanItems(int, int64_t start, int64_t end)
{
	const char *const name = kNames[P].run;
	static const zx::telemetry::Counter run = zx::telemetry::Register(zx::telemetry::CounterSpec{ name, "ms", zx::telemetry::CounterKind::Time, 0 });
	zx::telemetry::Region(run, start, end);
}

template <int P> void Waited(int64_t start, int64_t end)
{
	const char *const name = kNames[P].wait;
	static const zx::telemetry::Counter wait = zx::telemetry::Register(zx::telemetry::CounterSpec{ name, "ms", zx::telemetry::CounterKind::Time, 0 });
	zx::telemetry::Region(wait, start, end);
}

const JobObserver kTelemetry[] = {
	{ zx::telemetry::NowNs, WorkerStarted<0>, RanItems<0>, Waited<0> },
	{ zx::telemetry::NowNs, WorkerStarted<1>, RanItems<1>, Waited<1> },
};
const bool s_observed = (SetJobObserver(&kTelemetry[0]), true);

// [Doruk-Aksoy] pool made with want workers, or resized to them unless its caller may not block, its own observer set and its spin kept to fua_jobs_spin_us.
JobPool &Sized(JobPool *&pool, int want, const JobObserver *observer)
{
	if (want > kMaxWorkers) want = kMaxWorkers;
	if (pool == NULL)
	{
		pool = new JobPool(want);
		pool->SetObserver(observer);
	}
	else
	{
		want = ComputePoolWorkers(want, pool->Workers(), !zx::mainloop::Stepped());
		if (want != pool->Workers()) pool->Resize(want);
	}
	if (pool->SpinMicros() != fua_jobs_spin_us) pool->SetSpinMicros(fua_jobs_spin_us);
	return *pool;
}
}

JobPool &Pool()
{
	int want = fua_jobs_threads;
	if (want < 0) want = ComputeAutoWorkers((int)std::thread::hardware_concurrency(), 7, fua_gamethread ? 2 : 1);
	return Sized(g_pool, want, NULL);
}

JobPool &TicPool()
{
	int want = fua_jobs_tic_threads;
	if (want < 0) want = ComputeAutoWorkers((int)std::thread::hardware_concurrency(), kTicWorkersCap, 1);
	return Sized(g_ticPool, want, &kTelemetry[1]);
}

}} // namespace zx::jobs
