// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/computation/lock_compute.h"

namespace zx {

namespace {
std::atomic<LockNestedFn> g_nested(nullptr);
thread_local const Lock *t_held = nullptr;
}

void SetLockNestedListener(LockNestedFn listen)
{
	g_nested.store(listen);
}

const Lock *HeldLock()
{
	return t_held;
}

void Lock::lock()
{
	if (t_held != nullptr)
	{
		const LockNestedFn listen = g_nested.load();
		if (listen != nullptr) listen(m_name, t_held->m_name);
	}
	m_takes.fetch_add(1, std::memory_order_relaxed);
	if (!m_mutex.try_lock())
	{
		m_waits.fetch_add(1, std::memory_order_relaxed);
		m_mutex.lock();
	}
	// [rc4l] A nested take keeps the outer lock as the one held, so its unlock still clears it.
	if (t_held == nullptr) t_held = this;
}

void Lock::unlock()
{
	if (t_held == this) t_held = nullptr;
	m_mutex.unlock();
}

} // namespace zx
