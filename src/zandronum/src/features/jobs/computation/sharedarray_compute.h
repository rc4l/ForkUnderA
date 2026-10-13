// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] An array the lanes of one job write at once: every element atomic and accessed relaxed, so marks and stamps need no lock and no lane loses another's write.

#ifndef ZX_SHAREDARRAY_COMPUTE_H
#define ZX_SHAREDARRAY_COMPUTE_H

#include <atomic>
#include <memory>
#include <stddef.h>

namespace zx { namespace jobs {

template <class T> class SharedArray
{
public:
	// [rc4l] Size it and set every element to `fill`; only between jobs.
	void Resize(size_t count, T fill)
	{
		m_data.reset(count ? new std::atomic<T>[count] : NULL);
		m_size = count;
		Fill(fill);
	}
	void Fill(T value)
	{
		for (size_t i = 0; i < m_size; i++) m_data[i].store(value, std::memory_order_relaxed);
	}
	void Clear() { Resize(0, T()); }
	size_t Size() const { return m_size; }

	T Load(size_t i) const { return m_data[i].load(std::memory_order_relaxed); }
	void Store(size_t i, T value) { m_data[i].store(value, std::memory_order_relaxed); }
	void Or(size_t i, T bits) { m_data[i].fetch_or(bits, std::memory_order_relaxed); }

	// [rc4l] Lower element i to `value` when it holds more or is from another epoch (the bits under `epochMask`): what a race among lanes for the smallest key leaves, whoever runs last.
	void LowerWithin(size_t i, T value, T epochMask)
	{
		T old = Load(i);
		while (((old ^ value) & epochMask) != 0 || value < old)
			if (m_data[i].compare_exchange_weak(old, value, std::memory_order_relaxed)) return;
	}

private:
	std::unique_ptr<std::atomic<T>[]> m_data;
	size_t m_size = 0;
};

}} // namespace zx::jobs

#endif // ZX_SHAREDARRAY_COMPUTE_H
