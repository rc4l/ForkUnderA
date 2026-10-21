// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] The one queue type that carries work between the game and the main thread: values only, drained in the order pushed, every queue listed with its depth and high-water mark.

#ifndef ZX_QUEUE_COMPUTE_H
#define ZX_QUEUE_COMPUTE_H

#include "features/handoff/computation/lock_compute.h"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace zx {

// [rc4l] What every queue reports, whatever it carries.
class QueueBase
{
public:
	explicit QueueBase(const char *name);
	virtual ~QueueBase();
	QueueBase(const QueueBase &) = delete;
	QueueBase &operator=(const QueueBase &) = delete;
	const char *Name() const { return m_name; }
	virtual size_t Depth() const = 0;
	virtual size_t HighWater() const = 0;

	// [rc4l] Calls each(queue) for every queue alive, in the order they were made.
	static void ForEach(void (*each)(const QueueBase &queue, void *user), void *user);

private:
	const char *m_name;
};

template <class T> class Queue : public QueueBase
{
	static_assert(!std::is_pointer<T>::value, "a queue carries values, never pointers into the other thread's data");

public:
	explicit Queue(const char *name) : QueueBase(name), m_lock(name), m_high(0) {}

	void Push(T value)
	{
		LockScope hold(m_lock);
		m_items.push_back(std::move(value));
		if (m_items.size() > m_high) m_high = m_items.size();
	}

	// [rc4l] Hands each item pushed so far to take, oldest first, outside the lock so take may push again; returns how many it took.
	template <class F> size_t Drain(F &&take)
	{
		{
			LockScope hold(m_lock);
			m_draining.swap(m_items);
		}
		const size_t n = m_draining.size();
		for (size_t i = 0; i < n; i++) take(m_draining[i]);
		m_draining.clear();
		return n;
	}

	size_t Depth() const override
	{
		LockScope hold(m_lock);
		return m_items.size();
	}

	size_t HighWater() const override
	{
		LockScope hold(m_lock);
		return m_high;
	}

private:
	mutable Lock m_lock;
	std::vector<T> m_items;
	std::vector<T> m_draining;
	size_t m_high;
};

} // namespace zx

#endif // ZX_QUEUE_COMPUTE_H
