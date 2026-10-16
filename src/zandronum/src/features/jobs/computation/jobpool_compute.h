// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A fork-join pool for a frame's parallel work: the caller and a fixed set of workers share out the indices of one job and the caller returns when every index has run.

#ifndef ZX_JOBPOOL_COMPUTE_H
#define ZX_JOBPOOL_COMPUTE_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace zx { namespace jobs {

// [rc4l] Who hears every pool's work, which features/jobs points at features/telemetry: a worker's first job, a lane's run of items and the caller's wait for the rest, timed on the observer's own clock.
struct JobObserver
{
	int64_t (*now)();
	void (*workerStarted)(int lane);
	void (*ranItems)(int lane, int64_t start, int64_t end);
	void (*waited)(int64_t start, int64_t end);
};

// [rc4l] Sets the observer every pool reports to, NULL for none.
void SetJobObserver(const JobObserver *observer);

class JobPool
{
public:
	typedef void (*IndexFn)(void *user, int index, int lane);

	// [rc4l] `spinMicros` is how long an idle worker polls for the next job before it sleeps.
	explicit JobPool(int workers = 0, int spinMicros = 200);
	~JobPool();

	// [rc4l] Stop the workers and start `workers` new ones; never call it from inside a job.
	void Resize(int workers);
	int Workers() const { return (int)m_threads.size(); }
	// [rc4l] Lanes a job runs on, the caller included, so per-lane scratch can be sized before a job.
	int Lanes() const { return Workers() + 1; }
	// [rc4l] Workers asleep waiting for a job, past their spin.
	int Sleeping() const { return m_sleeping.load(); }
	// [rc4l] How long an idle worker polls for the next job before it sleeps, from its next idle spell on.
	void SetSpinMicros(int micros) { m_spinMicros.store(micros < 0 ? 0 : micros); }
	int SpinMicros() const { return m_spinMicros.load(); }

	// [rc4l] Run fn(user, index, lane) once for each index in [0, count): the caller is lane 0 and each worker its own lane, and the call returns once every index has run.
	void ParallelFor(int count, IndexFn fn, void *user);

	template <class F> void ParallelFor(int count, F &f)
	{
		ParallelFor(count, &Trampoline<F>, &f);
	}

private:
	template <class F> static void Trampoline(void *user, int index, int lane) { (*(F *)user)(index, lane); }

	void WorkerMain(int lane);
	// [rc4l] Poll for a job after generation `seen` or a stop for the spin's length: true if one came.
	bool SpinFor(unsigned seen);
	void Sleep(unsigned seen);
	void RunItems(int lane);
	void Stop();

	std::vector<std::thread> m_threads;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::atomic<int> m_spinMicros;

	// [rc4l] The job in flight, written only while no worker is inside one (m_active is 0) and published by m_open and m_generation.
	IndexFn m_fn;
	void *m_user;
	int m_count;
	std::atomic<int> m_next;
	std::atomic<int> m_done;
	std::atomic<bool> m_open;
	std::atomic<unsigned> m_generation;
	std::atomic<int> m_active;
	std::atomic<int> m_sleeping;
	std::atomic<bool> m_stop;
};

// [rc4l] The workers a pool gets by default on a machine with `hardwareThreads` logical processors: half of them less the caller, at most `cap`, none on a machine with two or fewer.
int ComputeAutoWorkers(int hardwareThreads, int cap);

}} // namespace zx::jobs

#endif // ZX_JOBPOOL_COMPUTE_H
