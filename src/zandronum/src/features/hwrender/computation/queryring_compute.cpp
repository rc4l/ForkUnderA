// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/hwrender/computation/queryring_compute.h"

namespace zx { namespace hwrender {

// [rc4l] Defined once here, since a test that takes it by reference needs it to exist outside the class.
const int QueryRing::kSlots;

int QueryRing::Begin()
{
	if (m_waiting == kSlots) return -1;
	const int slot = m_head;
	m_head = (m_head + 1) % kSlots;
	m_waiting++;
	return slot;
}

double ComputeQueryMs(unsigned long long ticks, unsigned long long frequency)
{
	return frequency == 0 ? -1.0 : 1000.0 * (double)ticks / (double)frequency;
}

}} // namespace zx::hwrender
