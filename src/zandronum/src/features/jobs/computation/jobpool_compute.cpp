// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/jobs/computation/jobpool_compute.h"

#include <chrono>

namespace zx { namespace jobs {

namespace {

std::atomic<const JobObserver *> g_observer{ nullptr };

} // namespace

void SetJobObserver(const JobObserver *observer)
{
	g_observer.store(observer, std::memory_order_release);
}

JobPool::JobPool(int workers, int spinMicros)
	: m_spinMicros(spinMicros), m_fn(NULL), m_user(NULL), m_count(0), m_next(0), m_done(0), m_open(false),
	  m_generation(0), m_active(0), m_sleeping(0), m_stop(false)
{
	Resize(workers);
}

JobPool::~JobPool()
{
	Stop();
}

void JobPool::Stop()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop.store(true);
	}
	m_wake.notify_all();
	for (size_t i = 0; i < m_threads.size(); i++) m_threads[i].join();
	m_threads.clear();
	m_stop.store(false);
}

void JobPool::Resize(int workers)
{
	Stop();
	for (int i = 0; i < workers; i++) m_threads.push_back(std::thread(&JobPool::WorkerMain, this, i + 1));
}

bool JobPool::SpinFor(unsigned seen)
{
	const std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + std::chrono::microseconds(m_spinMicros.load());
	for (int spins = 0; ; spins++)
	{
		if (m_generation.load() != seen || m_stop.load()) return true;
		// [rc4l] The clock is read every 64 polls, so a short spin still polls 64 times.
		if ((spins & 63) == 63 && std::chrono::steady_clock::now() >= until) return false;
		std::this_thread::yield();
	}
}

void JobPool::Sleep(unsigned seen)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_sleeping.fetch_add(1);
	m_wake.wait(lock, [this, seen] { return m_generation.load() != seen || m_stop.load(); });
	m_sleeping.fetch_sub(1);
}

void JobPool::WorkerMain(int lane)
{
	unsigned seen = m_generation.load();
	bool announced = false;
	for (;;)
	{
		if (!SpinFor(seen)) Sleep(seen);
		if (m_stop.load()) return;
		const unsigned g = m_generation.load();
		seen = g;
		// [rc4l] Joining before looking at the job lets the caller wait for every worker that may still touch it; one that joins after the job closed leaves at once.
		m_active.fetch_add(1);
		const JobObserver *o = g_observer.load(std::memory_order_acquire);
		if (o != nullptr && !announced) { o->workerStarted(lane); announced = true; }
		if (m_open.load() && m_generation.load() == g) RunItems(lane);
		m_active.fetch_sub(1);
	}
}

void JobPool::RunItems(int lane)
{
	const JobObserver *o = g_observer.load(std::memory_order_acquire);
	const int64_t start = o != nullptr ? o->now() : 0;
	int ran = 0;
	for (int i = m_next.fetch_add(1); i < m_count; i = m_next.fetch_add(1))
	{
		m_fn(m_user, i, lane);
		m_done.fetch_add(1);
		ran++;
	}
	if (o != nullptr && ran > 0) o->ranItems(lane, start, o->now());
}

void JobPool::ParallelFor(int count, IndexFn fn, void *user)
{
	if (m_threads.empty() || count <= 1)
	{
		for (int i = 0; i < count; i++) fn(user, i, 0);
		return;
	}
	m_fn = fn;
	m_user = user;
	m_count = count;
	m_next.store(0);
	m_done.store(0);
	m_open.store(true);
	m_generation.fetch_add(1);
	// [rc4l] Taking the lock orders the new generation before a sleeper's check of it, so no wake is lost.
	if (m_sleeping.load() > 0) { std::lock_guard<std::mutex> lock(m_mutex); }
	m_wake.notify_all();
	RunItems(0);
	const JobObserver *o = g_observer.load(std::memory_order_acquire);
	const int64_t waitStart = o != nullptr ? o->now() : 0;
	while (m_done.load() < count) std::this_thread::yield();
	m_open.store(false);
	while (m_active.load() != 0) std::this_thread::yield();
	if (o != nullptr) o->waited(waitStart, o->now());
}

int ComputeAutoWorkers(int hardwareThreads, int cap)
{
	const int workers = hardwareThreads / 2 - 1;
	if (workers <= 0) return 0;
	return workers < cap ? workers : cap;
}

}} // namespace zx::jobs
