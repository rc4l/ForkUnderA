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

// [Doruk-Aksoy] One job as its caller saw it: when it opened, the caller's wait for the rest, the workers asleep as it opened, and the workers that ran an item with the latest one's start (opened when none did).
struct JobWait
{
	int64_t opened, start, end;
	int asleep, lanes;
	int64_t lastStart;
};

// [rc4l] Who hears every pool's work, which features/jobs points at features/telemetry: a worker's first job, a lane's run of items and the caller's wait for the rest, timed on the observer's own clock.
struct JobObserver
{
	int64_t (*now)();
	void (*workerStarted)(int lane);
	void (*ranItems)(int lane, int64_t start, int64_t end);
	void (*waited)(const JobWait &wait);
};

// [Doruk-Aksoy] Adds to ran the workers whose start is not negative (none ran), and raises last to the latest of those starts.
void ComputeLaneStarts(const int64_t *starts, int workers, int &ran, int64_t &last);

// [rc4l] Sets the observer every pool reports to, NULL for none.
void SetJobObserver(const JobObserver *observer);

// [rc4l] How many chunks of at most chunk items cover count items, none for nothing to do.
int ComputeChunkCount(int count, int chunk);

// [rc4l] The items [lo, hi) chunk number index covers, the last one cut short at count.
void ComputeChunkBounds(int count, int chunk, int index, int &lo, int &hi);

// [Doruk-Aksoy] The chunk a job splits count items by on a pool of workers: chunk, or all of them at once on a pool of none, so a gather by parts makes the one part a single thread does.
int ComputeChunkFor(int count, int chunk, int workers);

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
	// [Doruk-Aksoy] The observer this pool reports to in place of the one SetJobObserver sets, NULL for that one.
	void SetObserver(const JobObserver *observer) { m_observer.store(observer, std::memory_order_release); }

	// [rc4l] Run fn(user, index, lane) once for each index in [0, count): the caller is lane 0 and each worker its own lane, and the call returns once every index has run.
	void ParallelFor(int count, IndexFn fn, void *user);

	template <class F> void ParallelFor(int count, F &f)
	{
		ParallelFor(count, &Trampoline<F>, &f);
	}

	// [Doruk-Aksoy] Wakes the sleeping workers with no job, so they spin for the one the caller opens next; from the caller's thread only, as ParallelFor.
	void Wake();

	// [rc4l] Run f(lo, hi, lane) over [0, count) in chunks of at most chunk items, so a caller splits a scan without working the bounds out itself.
	template <class F> void ParallelChunks(int count, int chunk, F &f)
	{
		struct Chunks
		{
			F &f;
			int count, chunk;
			void operator()(int index, int lane) const { int lo, hi; ComputeChunkBounds(count, chunk, index, lo, hi); f(lo, hi, lane); }
		} chunks = { f, count, chunk };
		ParallelFor(ComputeChunkCount(count, chunk), chunks);
	}

private:
	template <class F> static void Trampoline(void *user, int index, int lane) { (*(F *)user)(index, lane); }

	void WorkerMain(int lane);
	// [rc4l] Poll for a job after generation `seen` or a stop for the spin's length: true if one came.
	bool SpinFor(unsigned seen);
	void Sleep(unsigned seen);
	void RunItems(int lane);
	void Stop();
	const JobObserver *Observer() const;

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
	std::atomic<const JobObserver *> m_observer;
	// [Doruk-Aksoy] When each worker began the job in flight, -1 until it runs an item, read by the caller once every worker has left it.
	std::vector<int64_t> m_laneStart;
};

// [rc4l] The workers a pool gets by default on a machine with `hardwareThreads` logical processors: half of them less the `reserved` threads that run beside it (the caller, and the game thread when it is on), at most `cap`, none when that leaves none.
int ComputeAutoWorkers(int hardwareThreads, int cap, int reserved);

// [rc4l] The workers a running pool of `have` keeps when `want` are asked for: resizing joins them, so a caller that may not block keeps what it has, since in a browser a worker still starting needs the main thread that would be waiting for it.
int ComputePoolWorkers(int want, int have, bool mayBlock);

}} // namespace zx::jobs

#endif // ZX_JOBPOOL_COMPUTE_H
