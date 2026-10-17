// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] A call one thread hands another and waits on, for work only the main thread may do whose answer a tic on the game thread needs at once, such as loading a font.

#ifndef ZX_ASKS_COMPUTE_H
#define ZX_ASKS_COMPUTE_H

#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>

namespace zx { namespace handoff {

class Asks
{
public:
	typedef void (*Fn)(void *arg);

	// [rc4l] Has the serving thread run fn(arg) and returns once it has, rethrowing what it threw; asks take turns.
	void Ask(Fn fn, void *arg);
	// [rc4l] Runs the ask waiting, if any; true when one ran.
	bool Serve();
	// [rc4l] Serves asks until done() holds, read under the asks' lock, so whoever makes it true calls Wake after.
	template <typename Done> void ServeUntil(Done done)
	{
		std::unique_lock<std::mutex> hold(m_mutex);
		for (;;)
		{
			if (RunTaken(hold)) continue;
			if (done()) return;
			m_toServer.wait(hold);
		}
	}
	// [rc4l] Wakes a ServeUntil to read its condition again.
	void Wake();
	uint64_t Served() const;

private:
	struct Pending
	{
		Fn fn;
		void *arg;
		bool done;
		std::exception_ptr error;
	};
	// [rc4l] Takes the waiting ask and runs it with the lock let go; false when none waits.
	bool RunTaken(std::unique_lock<std::mutex> &hold);

	mutable std::mutex m_mutex;
	std::condition_variable m_toServer, m_toAsker;
	Pending *m_pending = nullptr;
	uint64_t m_served = 0;
};

}} // namespace zx::handoff

#endif // ZX_ASKS_COMPUTE_H
