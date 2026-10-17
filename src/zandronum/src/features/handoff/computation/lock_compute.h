// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The one lock type for state both threads touch: named, counting how often it had to wait, and refusing to be taken while the thread already holds another, so no two locks can ever be taken in opposite orders.

#ifndef ZX_LOCK_COMPUTE_H
#define ZX_LOCK_COMPUTE_H

#include <atomic>
#include <cstdint>
#include <mutex>

namespace zx {

// [rc4l] Who hears a thread take a lock while it holds another, which features/handoff counts and reports.
typedef void (*LockNestedFn)(const char *taking, const char *held);
void SetLockNestedListener(LockNestedFn listen);

class Lock
{
public:
	explicit Lock(const char *name) : m_name(name), m_takes(0), m_waits(0) {}
	Lock(const Lock &) = delete;
	Lock &operator=(const Lock &) = delete;

	void lock();
	void unlock();
	const char *Name() const { return m_name; }
	uint64_t Takes() const { return m_takes.load(std::memory_order_relaxed); }
	// [rc4l] Takes that found the lock held by another thread.
	uint64_t Waits() const { return m_waits.load(std::memory_order_relaxed); }

private:
	const char *m_name;
	std::mutex m_mutex;
	std::atomic<uint64_t> m_takes;
	std::atomic<uint64_t> m_waits;
};

// [rc4l] The lock this thread holds, or NULL.
const Lock *HeldLock();

// [rc4l] Holds a Lock for its scope.
class LockScope
{
public:
	explicit LockScope(Lock &lock) : m_lock(lock) { m_lock.lock(); }
	~LockScope() { m_lock.unlock(); }
	LockScope(const LockScope &) = delete;
	LockScope &operator=(const LockScope &) = delete;

private:
	Lock &m_lock;
};

} // namespace zx

#endif // ZX_LOCK_COMPUTE_H
