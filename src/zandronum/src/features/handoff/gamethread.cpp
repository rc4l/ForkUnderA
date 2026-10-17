// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "features/handoff/gamethread.h"
#include "features/handoff/computation/asks_compute.h"
#include "features/handoff/effects.h"
#include "features/handoff/seam.h"
#include "features/telemetry/telemetry.h"
#include "mcp_bridge.h"   // [rc4l] MCP_Crash_NoteGameThread

#include <atomic>
#include <exception>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace zx { namespace handoff {

namespace {

std::mutex g_mutex;
std::condition_variable g_wake;
Asks g_asks;   // [rc4l] what the game thread waits on the main thread for, served while the main thread waits on it too
void (*g_work)() = nullptr;     // [rc4l] what the game thread runs next, under g_mutex
std::atomic<bool> g_done(true);
std::atomic<bool> g_inFlight(false);
std::atomic<bool> g_asking(false);
std::thread *g_thread = nullptr;
std::exception_ptr g_error;   // [rc4l] what the work threw, rethrown on the main thread when it is taken back

void GameThreadMain()
{
	NoteGameThread();
	zx::telemetry::NameThisThread("game");
	MCP_Crash_NoteGameThread();
	for (;;)
	{
		void (*work)() = nullptr;
		{
			std::unique_lock<std::mutex> hold(g_mutex);
			g_wake.wait(hold, [] { return g_work != nullptr; });
			work = g_work;
			g_work = nullptr;
		}
		// [rc4l] The thread running tics is whichever runs the one in flight, this one or the main thread inline.
		NoteGameThread();
		try
		{
			work();
		}
		catch (...)
		{
			g_error = std::current_exception();
		}
		g_done.store(true, std::memory_order_release);
		g_asks.Wake();
	}
}

} // namespace

void StartOnGameThread(void (*work)())
{
	if (g_thread == nullptr) g_thread = new std::thread(GameThreadMain);
	{
		std::lock_guard<std::mutex> hold(g_mutex);
		g_done.store(false, std::memory_order_relaxed);
		g_work = work;
	}
	g_inFlight.store(true, std::memory_order_relaxed);
	g_wake.notify_one();
}

bool GameThreadDone()
{
	return g_done.load(std::memory_order_acquire);
}

void WaitForGameThread()
{
	g_asks.ServeUntil([] { return g_done.load(std::memory_order_acquire); });
}

bool TicInFlight()
{
	return g_inFlight.load(std::memory_order_relaxed);
}

void TakeBack()
{
	g_inFlight.store(false, std::memory_order_relaxed);
	if (g_error)
	{
		std::exception_ptr error = g_error;
		g_error = nullptr;
		std::rethrow_exception(error);
	}
}

void AskMain(void (*fn)(void *), void *arg)
{
	if (!Deferring())
	{
		fn(arg);
		return;
	}
	struct Call { void (*fn)(void *); void *arg; } call = { fn, arg };
	g_asking.store(true);
	struct Asking { ~Asking() { g_asking.store(false); } } asking;
	g_asks.Ask([](void *p) {
		const Call &c = *(const Call *)p;
		// [rc4l] The game thread waits inside its tic meanwhile, so the main thread stands in for it as it would between tics, until the call returns or throws.
		struct StandIn
		{
			Phase was;
			StandIn() : was(CurrentPhase()) { SetGameRunning(false); SetMainInHandoff(true); }
			~StandIn() { SetGameRunning(was.gameRunning); SetMainInHandoff(was.mainInHandoff); }
		} standIn;
		c.fn(c.arg);
	}, &call);
}

bool GameThreadAsking()
{
	return g_asking.load();
}

void ServeAsks()
{
	if (g_asks.Serve()) ZX_COUNT("handoff/asks", "asks", 1);
}

}} // namespace zx::handoff
