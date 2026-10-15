// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] Which of a ring of GPU queries a frame times with and which finished ones to read, oldest first, so the CPU never waits on the GPU to learn its time (dggputimer.cpp).

#ifndef ZX_QUERYRING_COMPUTE_H
#define ZX_QUERYRING_COMPUTE_H

namespace zx { namespace hwrender {

class QueryRing
{
public:
	static const int kSlots = 8;
	// [rc4l] The slot to time this frame in, or -1 when every slot still waits to be read.
	int Begin();
	// [rc4l] Reads the waiting slots oldest first while read(slot) says its result is in, the count read.
	template <class Read>
	int Collect(Read read)
	{
		int n = 0;
		while (m_waiting > 0 && read(m_tail))
		{
			m_tail = (m_tail + 1) % kSlots;
			m_waiting--;
			n++;
		}
		return n;
	}

private:
	int m_head = 0, m_tail = 0, m_waiting = 0;
};

// [rc4l] A duration query's ticks at frequency as milliseconds, or -1 for a frequency of 0, which a query that never ran reports.
double ComputeQueryMs(unsigned long long ticks, unsigned long long frequency);

}} // namespace zx::hwrender

#endif // ZX_QUERYRING_COMPUTE_H
