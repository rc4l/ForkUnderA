// SPDX-License-Identifier: GPL-3.0-or-later

#include "features/dtoa-lock/dtoalock.h"

#include <atomic>
#include <thread>

namespace {

// [MGOOOOOO] Spin locks, as these are set before any constructor runs and float CVar defaults format numbers during static init.
std::atomic_flag s_locks[2] = { ATOMIC_FLAG_INIT, ATOMIC_FLAG_INIT };

} // namespace

extern "C" void fua_dtoa_lock(int n)
{
	while (s_locks[n].test_and_set(std::memory_order_acquire)) std::this_thread::yield();
}

extern "C" void fua_dtoa_unlock(int n)
{
	s_locks[n].clear(std::memory_order_release);
}
